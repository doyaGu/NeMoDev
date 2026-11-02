#include "ui/controls/ParameterBox.h"
#include "ui/native/ParameterCopiesDetail.h"
#include "core/graph/ParameterValues.h"
#include "core/graph/ParameterPicking.h"
#include "core/graph/RuntimeGraph.h"
#include "core/graph/ValueCommands.h"
#include "core/catalog/ValueText.h"
#include "core/document/Edit.h"
#include "core/runtime/TextEncoding.h"
#include "features/values/ParameterCommands.h"
#include "workspace/EditorClient.h"
#include <algorithm>

namespace vdev {

namespace {

std::vector<CK_ID> MembersOf(const Document &document, CKParameter *parameter) {
    std::vector<ParameterMember> members; std::vector<CK_ID> ids;
    if (ParameterMembers(document, parameter, members)) for (const auto &member : members)
        ids.push_back(document.NativeId(member.parameter, Document::ObjectScope::Existing));
    return ids;
}

// The values held by pointer (the types' own memory, copied by their copy
// functions): a dependencies value, an object array.
template <typename T> T *HeldOf(CKParameter *parameter) {
    T *held = nullptr;
    if (parameter && parameter->GetDataSize() == static_cast<int>(sizeof(held))) parameter->GetValue(&held, FALSE);
    return held;
}

} // namespace


ParameterCopies::ParameterCopies(CkRuntimeHost &host, EditorWorkspace &workspace, std::function<Document *()> document,
                                 std::function<ParameterDependencies()> copyOptions, std::function<void()> clearSelection)
    : m_Data(new Data{host, workspace, std::move(document), std::move(copyOptions), std::move(clearSelection), {}}) {}
ParameterCopies::~ParameterCopies() = default;
EditorApplication &ParameterCopies::Editor() const { return m_Data->workspace.Application(); }
bool ParameterCopies::Current(ObjectRef ref) const {
    return Editor().NativeAvailable() && m_Data->owned.count(ref) && m_Data->Parameter(ref);
}
bool ParameterCopies::Read(ObjectRef ref, ParameterValue &value) const {
    ParameterValueDescription description;
    if (!Current(ref) || !DescribeParameterValue(*m_Data->document(), m_Data->Parameter(ref), description)) return false;
    value = std::move(description.value); return true;
}
bool ParameterCopies::Parse(ObjectRef ref, const EditorValue &field, ParameterValue &result) const {
    ParameterValueDescription description;
    ParameterValue value;
    if (!Current(ref) || !DescribeParameterValue(*m_Data->document(), m_Data->Parameter(ref), description) ||
        !ParameterValueFromEditor(description, field, value)) return false;
    result = std::move(value);
    return true;
}
bool ParameterCopies::Restore(ObjectRef ref, const ParameterValue &value) {
    return Current(ref) && WriteParameterDraft(*m_Data->document(), m_Data->Parameter(ref), value);
}
std::string ParameterCopies::ReadText(ObjectRef ref) const {
    return Current(ref) ? AnsiToUtf8(ParameterText(m_Data->Parameter(ref))) : std::string();
}
bool ParameterCopies::WriteText(ObjectRef ref, const std::string &text) {
    return Current(ref) && m_Data->Parameter(ref)->SetStringValue(const_cast<CKSTRING>(Utf8ToAnsi(text).c_str())) == CK_OK;
}
std::vector<std::pair<int32_t, std::string>> ParameterCopies::Classes(int32_t type) const {
    return Editor().NativeAvailable() ? DerivedClasses(type) : std::vector<std::pair<int32_t, std::string>>{};
}
ObjectRef ParameterCopies::Pick(float x, float y, int32_t type) const {
    auto *document = m_Data->document();
    if (!document || !Editor().NativeAvailable() || !type) return {};
    for (const auto ref : m_Data->workspace.Pick(x, y, nullptr, uint32_t(type)))
        if (auto *object = document->Resolve(ref); object && CKIsChildClassOf(object->GetClassID(), type)) return ref;
    return {};
}

void ParameterBox::Clear() {
    CKContext *context = m_Copies.m_Data->host.Context();
    // The working copies of an earlier generation went with ClearAll; their ids may be another object's now.
    const bool alive = m_Generation == m_Copies.m_Data->host.Generation();
    for (const Row &row : m_Rows)
        if (alive && row.owned && context && m_Copies.m_Data->document() && m_Copies.m_Data->document()->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing)) context->DestroyObject(row.work);
    for (const auto &row : m_Rows) ForgetRow(row);
    m_Rows.clear();
    m_Editors.clear();
    m_CopyProvider.clear();
    EndPicking();
    m_PickWork = 0;
}

bool ParameterBox::AddDefault(uint64_t type, const std::string &label) {
    const CKGUID guid(CKDWORD(type >> 32), CKDWORD(type));
    if (auto *work = m_Copies.m_Data->Parameter(WorkReference()); work && work->GetGUID() == guid)
        return true;
    Clear();
    auto *context = m_Copies.m_Data->host.Context();
    if (!context || context->GetParameterManager()->ParameterGuidToType(guid) < 0)
        return false;
    auto *work = context->CreateCKParameterOut(Utf8ToAnsi(label).c_str(), guid, TRUE);
    if (!work)
        return false;
    Row row;
    row.owned = true;
    row.label = label;
    InitRow(row, m_Copies.m_Data->document()->Reference(work->GetID()));
    AddRow(std::move(row));
    return true;
}

bool ParameterBox::AddDraft(const ParameterBox &source, const std::string &label) {
    return Add(source.WorkReference(), label);
}

bool ParameterBox::CopyDraft(const ParameterBox &source) {
    auto *to = m_Copies.m_Data->Parameter(WorkReference()), *from = m_Copies.m_Data->Parameter(source.WorkReference());
    if (!to || !from || !to->IsDynamic() || !from->IsDynamic() || to->GetGUID() != from->GetGUID() ||
        to->CopyValue(from, FALSE) != CK_OK)
        return false;
    InitRow(m_Rows.front(), WorkReference());
    return true;
}

bool ParameterBox::AddDependencies(const std::string &label, uint64_t packedType, const ParameterDependencies &value) {
    CKContext *context = m_Copies.m_Data->host.Context();
    if (!context) return false;
    const CKGUID type(CKDWORD(packedType >> 32), CKDWORD(packedType));
    CKParameterOut *work = context->CreateCKParameterOut(const_cast<CKSTRING>(Utf8ToAnsi(label).c_str()), type, TRUE);
    CKDependencies copy; copy.m_Flags = CK_DEPENDENCIES_FLAGS(value.flags);
    copy.Resize(int(value.options.size()));
    for (size_t i = 0; i < value.options.size(); ++i) copy[int(i)] = value.options[i];
    if (!SetHeldValue(work, &copy)) {
        if (work) context->DestroyObject(work);
        return false;
    }
    Row row;
    row.owned = true;
    row.label = label;
    InitRow(row, m_Copies.m_Data->document()->Reference(work->GetID()));
    AddRow(std::move(row));
    return true;
}

bool ParameterBox::Dependencies(size_t row, ParameterDependencies &value) const {
    CKDependencies *held = HeldOf<CKDependencies>(m_Copies.m_Data->Parameter(WorkReference(row)));
    if (!held) return false;
    value.flags = uint32_t(held->m_Flags); value.options.resize(size_t(held->Size()));
    for (size_t i = 0; i < value.options.size(); ++i) value.options[i] = (*held)[int(i)];
    return true;
}

void ParameterBox::InitRow(Row &row, ObjectRef reference) {
    ForgetRow(row);
    unsigned remaining = 65536;
    InitRow(row, reference, 0, remaining);
    ShareValues(row);
}
void ParameterBox::ForgetRow(const Row &row) {
    m_Copies.m_Data->owned.erase(row.workReference);
    for (const auto &member : row.members) ForgetRow(member);
}

void ParameterBox::InitRow(Row &row, ObjectRef reference, unsigned depth, unsigned &remaining) {
    auto *work = m_Copies.m_Data->Parameter(reference);
    if (!work) { row.invalid = true; return; }
    if (depth >= 32 || !remaining) { row.invalid = true; return; }
    m_Copies.m_Data->owned.insert(reference);
    --remaining;
    CKContext *context = m_Copies.m_Data->host.Context();
    CKParameterManager *manager = context->GetParameterManager();
    row.work = work->GetID();
    row.workReference = m_Copies.m_Data->document() ? m_Copies.m_Data->document()->Reference(row.work) : ObjectRef{};
    ParameterValueDescription description;
    row.typed = m_Copies.m_Data->document() && DescribeParameterValue(*m_Copies.m_Data->document(), work, description);
    if (row.typed) { row.valueRoot = std::make_unique<ParameterValue>(std::move(description.value)); row.valueOwner = row.valueRoot.get(); }
    if (row.typed) {
        auto &app = m_Copies.Editor();
        if (const auto *property = app.Property(ParameterProperty(description.value.kind))) {
            row.property = *property;
        }
    }
    row.info = ParameterUIOf(manager, work->GetType());
    switch (row.info.ui) {
    case ParameterUI::Object:
        // Dev's generic classes show the class filter (sub_24E6C7E0).
        row.filterClass = row.info.pickClass;
        row.extended = row.filterClass == CKCID_OBJECT || row.filterClass == CKCID_BEOBJECT;
        break;
    case ParameterUI::Struct: {
        std::vector<ParameterMember> members;
        if (!ParameterMembers(*m_Copies.m_Data->document(), work, members)) { row.invalid = true; break; }
        row.members.clear();
        for (const auto &entry : members) {
            auto *member = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(entry.parameter, CKCID_PARAMETER, Document::ObjectScope::Existing));
            if (!member) { row.invalid = true; break; }
            Row child; child.label = entry.name;
            InitRow(child, entry.parameter, depth + 1, remaining);
            row.members.push_back(std::move(child));
        }
        break;
    }
    default: break;
    }
}

bool ParameterBox::ValuesCurrent(const Row &row) const {
    if (row.typed) {
        auto *work = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
        ParameterValueDescription description;
        const auto *remembered = row.References();
        if (!remembered || !DescribeParameterValue(*m_Copies.m_Data->document(), work, description)) return false;
        auto value = ParameterDraftValue(std::move(description.value), *remembered);
        auto *target = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(row.targetReference, CKCID_PARAMETER));
        return DescribeParameterValue(*m_Copies.m_Data->document(), target, description) && ValidateParameterValue(*m_Copies.m_Data->document(), description, value);
    }
    for (const auto &member : row.members) if (!ValuesCurrent(member)) return false;
    return true;
}

void ParameterBox::TargetRow(Row &row, ObjectRef reference) {
    auto *target = m_Copies.m_Data->Parameter(reference);
    row.target = target ? target->GetID() : 0;
    row.targetReference = m_Copies.m_Data->document()->Reference(row.target);
    if (!target) { row.invalid = true; return; }
    auto *work = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
    if (!work || work->GetGUID() != target->GetGUID()) return; // a pending type change
    if (row.info.ui != ParameterUI::Struct) return;
    std::vector<ParameterMember> members;
    if (!ParameterMembers(*m_Copies.m_Data->document(), target, members) || members.size() != row.members.size()) { row.invalid = true; return; }
    for (size_t i = 0; i < members.size(); ++i)
        TargetRow(row.members[i], members[i].parameter);
    ParameterValueDescription description;
    if (DescribeParameterValue(*m_Copies.m_Data->document(), target, description)) row.schema = ParameterFields(description).back();
}

bool ParameterBox::TargetsCurrent(const Row &row) const {
    auto *target = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(row.targetReference, CKCID_PARAMETER));
    auto *work = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
    if (!target || !work || target->GetGUID() != work->GetGUID()) return false;
    if (row.info.ui == ParameterUI::Struct) {
        std::vector<ParameterMember> members;
        if (!ParameterMembers(*m_Copies.m_Data->document(), target, members) || members.size() != row.members.size()) return false;
        for (size_t i = 0; i < members.size(); ++i)
            if (members[i].parameter != row.members[i].targetReference || !TargetsCurrent(row.members[i])) return false;
    }
    return true;
}

bool ParameterBox::Add(ObjectRef reference, const std::string &label, uint64_t packedType) {
    auto *target = m_Copies.m_Data->Parameter(reference);
    const CKGUID type(CKDWORD(packedType >> 32), CKDWORD(packedType));
    CKContext *context = m_Copies.m_Data->host.Context();
    if (!context || !target) return false;
    // The working copy (CK2UI sub_24E64940: a CKParameterOut of the type).
    const bool other = (type.d1 || type.d2) && !(type == target->GetGUID());
    CKParameterOut *work =
        context->CreateCKParameterOut(const_cast<CKSTRING>(Utf8ToAnsi(label).c_str()), other ? type : target->GetGUID(), TRUE);
    if (!work) return false;
    if (!other) work->CopyValue(target, FALSE);
    Row row;
    row.target = target->GetID();
    row.targetReference = m_Copies.m_Data->document()->Reference(row.target);
    row.owned = true;
    row.label = label;
    InitRow(row, m_Copies.m_Data->document()->Reference(work->GetID()));
    TargetRow(row, reference);
    AddRow(std::move(row));
    return true;
}

void ParameterBox::AddRow(Row row) {
    // Rows left from an earlier generation name objects that are gone.
    if (m_Generation != m_Copies.m_Data->host.Generation()) {
        Clear();
        m_Generation = m_Copies.m_Data->host.Generation();
    }
    if (m_Editors.empty()) {
        auto &app = m_Copies.Editor();
        m_Editors = app.CaptureValueEditors(); m_CopyProvider = app.CommandProvider("parameter.copy-value");
    }
    m_Rows.push_back(std::move(row));
}

bool ParameterBox::Stale() {
    if (m_Rows.empty() || m_Generation == m_Copies.m_Data->host.Generation()) return false;
    Clear();
    return true;
}

std::string ParameterBox::Text(size_t row) const {
    auto *work = m_Copies.m_Data->Parameter(WorkReference(row));
    return work ? ParameterText(work) : std::string();
}

bool ParameterBox::PickResult(Row &row, ObjectRef picked) {
    auto *context = m_Copies.m_Data->host.Context();
    auto *work = m_Copies.m_Data->Parameter(row.workReference);
    if (!m_Copies.m_Data->document() ||
        !PickParameterDraft(*m_Copies.m_Data->document(), work, m_Copies.m_Data->document()->Resolve(picked, CKCID_OBJECT, Document::ObjectScope::Existing), m_PickShift,
                            row.extended ? row.filterClass : row.info.pickClass, row.extended ? row.derived : true,
                            row.extended ? Utf8ToAnsi(row.filter) : ""))
        return false;
    ParameterValueDescription description;
    if (row.typed && DescribeParameterValue(*m_Copies.m_Data->document(), work, description)) {
        if (auto *value = row.References())
            *value = std::move(description.value);
        row.beforeInteraction = {};
    } else if (row.info.ui == ParameterUI::Dependencies || row.info.ui == ParameterUI::ObjectArray)
        InitRow(row, m_Copies.m_Data->document()->Reference(work->GetID()));
    row.invalid = false;
    return true;
}

bool ParameterBox::RowChanged(const Row &row) const {
    CKContext *context = m_Copies.m_Data->host.Context();
    auto *work = m_Copies.m_Data->Parameter(row.workReference);
    auto *target = m_Copies.m_Data->Parameter(row.targetReference);
    if (!work || !target) return false;
    if (row.info.ui == ParameterUI::Struct) {
        const std::vector<CK_ID> members = MembersOf(*m_Copies.m_Data->document(), target);
        for (size_t i = 0; i < row.members.size() && i < members.size(); ++i) {
            if (RowChanged(row.members[i])) return true;
        }
        return false;
    }
    if (row.typed) {
        ParameterValueDescription current, previous;
        return DescribeParameterValue(*m_Copies.m_Data->document(), work, current) &&
            DescribeParameterValue(*m_Copies.m_Data->document(), target, previous) &&
            ParameterDraftValue(std::move(current.value), *row.References()) != previous.value;
    }
    if (ManagesMemory(context, work->GetType())) return ParameterText(work) != ParameterText(target);
    return ParameterBytes(work) != ParameterBytes(target);
}

bool ParameterBox::Changed() const {
    for (const Row &row : m_Rows)
        if ((row.targetReference && !TargetsCurrent(row)) || RowChanged(row)) return true;
    return false;
}

size_t ParameterBox::ApplyRow(const Row &row) {
    auto *work = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
    auto *target = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(row.targetReference, CKCID_PARAMETER));
    if (!work || !target || work->GetType() != target->GetType() || row.invalid ||
        (row.targetReference && !m_Copies.m_Data->document()->Resolve(row.targetReference)) ||
        !m_Copies.m_Data->document()->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing)) {
        m_ApplyFailed = true;
        return 0;
    }
    size_t steps = 0;
    ParameterValueDescription description;
    const bool described = DescribeParameterValue(*m_Copies.m_Data->document(), work, description);
    if (row.typed && !described) { m_ApplyFailed = true; return 0; }
    if (described) {
        if (auto *value = row.References()) description.value = ParameterDraftValue(std::move(description.value), *value);
        if (row.property.id.empty()) { m_ApplyFailed = true; return 0; }
        const auto fields = ParameterFields(description);
        const auto borrowed = BorrowEditorFields(fields);
        const auto value = borrowed.values.front();
        std::vector<EditorField> contextValues{fields[2]};
        if (description.value.kind == ParameterValueKind::Structure) contextValues.push_back(row.schema);
        auto &app = m_Copies.Editor();
        const auto result = app.ApplyEdit(row.property, app.Scope(), row.targetReference, value,
            contextValues);
        if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) {
            m_ApplyFailed = true;
            EditorClient(m_Copies.Editor().Host(), 0).Log(EDITOR_LOG_WARNING, "Edit Parameters: " + result.message);
        }
        return steps + (result.status == EDITOR_OK ? 1 : 0);
    }
    if (m_CopyProvider.empty()) { m_ApplyFailed = true; return 0; }
    const EditorValue values[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, row.targetReference},
        {EDITOR_TEXT("source"), EDITOR_OBJECT, row.workReference}};
    auto &app = m_Copies.Editor();
    const auto result = app.ApplyEdit("parameter.copy-value", {app.Scope(), values, 2, EditorText(m_CopyProvider)});
    if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) m_ApplyFailed = true;
    return steps + (result.status == EDITOR_OK ? 1 : 0);
}

size_t ParameterBox::Apply(CK_ID script) {
    if (!(m_Copies.m_Data->document() && m_Copies.m_Data->document()->IsOpen()) || Stale() || !Valid()) return 0;
    for (const auto &row : m_Rows) if (!TargetsCurrent(row) || !ValuesCurrent(row)) return 0;
    Edit edit(*m_Copies.m_Data->document(), "Edit Parameters");
    m_ApplyFailed = false;
    size_t steps = 0;
    for (const auto &row : m_Rows) {
        steps += ApplyRow(row);
        if (m_ApplyFailed) break;
    }
    if (m_ApplyFailed) { edit.Cancel(); return 0; }
    if (steps) edit.Touches(script);
    const auto result = edit.Commit();
    if (!result.ok && !result.Unchanged()) return 0;
    const auto accepted = [&](const auto &self, Row &row) -> void {
        row.beforeInteraction = {};
        for (auto &member : row.members) self(self, member);
    };
    for (auto &row : m_Rows) {
        accepted(accepted, row);
        if (!row.typed) continue;
        auto *target = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(row.targetReference, CKCID_PARAMETER));
        auto *work = CKParameter::Cast(m_Copies.m_Data->document()->Resolve(row.workReference, CKCID_PARAMETER, Document::ObjectScope::Existing));
        ParameterValueDescription value;
        if (target && work && DescribeParameterValue(*m_Copies.m_Data->document(), target, value) && WriteParameterDraft(*m_Copies.m_Data->document(), work, value.value))
            if (auto *remembered = row.References()) *remembered = std::move(value.value);
    }
    return steps;
}
} // namespace vdev
