#include "core/schematic/SchematicSearch.h"

#include <algorithm>
#include <cctype>

#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/graph/RuntimeGraph.h"
#include "core/graph/ValueCommands.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

// Names are compared and reported in UTF-8, the encoding of the UI query.
std::string NameOf(CKObject *object) {
    return object && object->GetName() ? AnsiToUtf8(object->GetName()) : std::string();
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

class Matcher {
public:
    Matcher(CKContext *context, const SearchQuery &query)
        : m_Context(context), m_Query(query),
          m_Text(query.caseSensitive && !query.listAll ? query.text : Lower(query.text)) {
        if (query.filter != SearchFilter::None && query.value)
            m_Value = CKParameter::Cast(context->GetObject(query.value));
    }

    // The CK ID filter, then the text (both when listAll).
    bool Matches(CK_ID id, const std::string &a, const std::string &b = std::string()) const {
        if (m_Query.id && id != m_Query.id) return false;
        if (m_Query.id && !m_Query.listAll) return true;
        if (m_Text.empty()) return m_Query.listAll;
        return Contains(a) || Contains(b);
    }

    // Elements that are not parameters show with no parameter filter only.
    bool Unfiltered() const { return m_Query.filter == SearchFilter::None; }

    // A parameter of the filter's type (and value): inputs and outputs for
    // Type, locals and shortcuts for both (sub_5B9BD0 ...).
    bool Takes(CKParameter *parameter, bool local) const {
        if (m_Query.filter == SearchFilter::None) return true;
        if (!parameter) return false;
        if (m_Query.filter == SearchFilter::Value && !local) return false;
        const int type = parameter->GetType();
        CKParameterManager *manager = m_Context->GetParameterManager();
        const bool typed = type == m_Query.type ||
                           (m_Query.derived && manager && m_Query.type >= 0 &&
                            manager->IsDerivedFrom(type, m_Query.type));
        if (!typed) return false;
        if (m_Query.filter == SearchFilter::Type) return true;
        return m_Value && ParameterText(parameter) == ParameterText(m_Value);
    }

    bool TakesInput(CKParameterIn *input) const {
        if (m_Query.filter == SearchFilter::None) return true;
        if (!input || m_Query.filter == SearchFilter::Value) return false;
        CKParameterManager *manager = m_Context->GetParameterManager();
        const int type = input->GetType();
        return type == m_Query.type ||
               (m_Query.derived && manager && m_Query.type >= 0 && manager->IsDerivedFrom(type, m_Query.type));
    }

private:
    bool Contains(const std::string &value) const {
        if (value.empty()) return false;
        const bool exact = m_Query.caseSensitive && !m_Query.listAll;
        return (exact ? value : Lower(value)).find(m_Text) != std::string::npos;
    }

    CKContext *m_Context;
    const SearchQuery &m_Query;
    std::string m_Text;
    CKParameter *m_Value = nullptr;
};

const BehaviorData *RecordOf(const InterfaceData *data, CK_ID id) {
    if (!data) return nullptr;
    if (id == data->rootBehavior.id) return &data->rootBehavior;
    for (const BehaviorData &record : data->behaviors)
        if (record.id == id) return &record;
    return nullptr;
}

void SearchBehavior(CKContext *context, CK_ID script, CKBehavior *behavior, const InterfaceData *data,
                    const Matcher &matcher, unsigned kinds, std::vector<SearchResult> &results) {
    if (!behavior) return;
    const CK_ID id = behavior->GetID();
    const bool isScript = id == script;
    CKBehavior *parent = behavior->GetParent();
    const CK_ID parentId = parent ? parent->GetID() : 0;
    auto add = [&](const ElementRef &ref, CK_ID focus, const char *kind, const std::string &label,
                   const std::string &detail, CK_ID object) {
        SearchResult result{script, ref, focus, kind, label, detail, object};
        results.push_back(result);
    };

    if (matcher.Unfiltered()) {
        const std::string name = NameOf(behavior);
        if (isScript) {
            if ((kinds & SearchScripts) && matcher.Matches(id, name))
                add({ElementKind::Script, id, id}, id, "Script", name, NameOf(behavior->GetOwner()), id);
        } else {
            const bool block = behavior->GetType() == CKBEHAVIORTYPE_BASE;
            const std::string prototype = behavior->GetPrototypeName() ? AnsiToUtf8(behavior->GetPrototypeName()) : "";
            if ((kinds & (block ? SearchBuildingBlocks : SearchGraphs)) && matcher.Matches(id, name, prototype))
                add({ElementKind::Behavior, parentId, id}, parentId, block ? "Building Block" : "Behavior Graph", name,
                    prototype, id);
        }
        if (kinds & SearchBehaviorInputs)
            for (int i = 0; i < behavior->GetInputCount(); ++i)
                if (CKBehaviorIO *io = behavior->GetInput(i))
                    if (matcher.Matches(io->GetID(), NameOf(io)))
                        add({ElementKind::Pin, id, id, i, -1, PinKind::BehaviorIn}, id, "Behavior Input", NameOf(io),
                            NameOf(behavior), io->GetID());
        if (kinds & SearchBehaviorOutputs)
            for (int i = 0; i < behavior->GetOutputCount(); ++i)
                if (CKBehaviorIO *io = behavior->GetOutput(i))
                    if (matcher.Matches(io->GetID(), NameOf(io)))
                        add({ElementKind::Pin, id, id, i, -1, PinKind::BehaviorOut}, id, "Behavior Output", NameOf(io),
                            NameOf(behavior), io->GetID());
    }
    if (kinds & SearchParameterInputs)
        for (int i = 0; i < behavior->GetInputParameterCount(); ++i) {
            CKParameterIn *input = behavior->GetInputParameter(i);
            if (input && matcher.TakesInput(input) && matcher.Matches(input->GetID(), NameOf(input)))
                add({ElementKind::Pin, id, id, i, -1, PinKind::ParamIn}, id, "Parameter Input", NameOf(input),
                    NameOf(behavior), input->GetID());
        }
    if (kinds & SearchParameterOutputs)
        for (int i = 0; i < behavior->GetOutputParameterCount(); ++i) {
            CKParameterOut *output = behavior->GetOutputParameter(i);
            if (output && matcher.Takes(output, false) && matcher.Matches(output->GetID(), NameOf(output)))
                add({ElementKind::Pin, id, id, i, -1, PinKind::ParamOut}, id, "Parameter Output", NameOf(output),
                    NameOf(behavior), output->GetID());
        }
    const BehaviorData *record = RecordOf(data, id);
    if (kinds & SearchLocalParameters)
        for (int i = 0; i < behavior->GetLocalParameterCount(); ++i) {
            CKParameterLocal *local = behavior->GetLocalParameter(i);
            if (local && matcher.Takes(local, true) && matcher.Matches(local->GetID(), NameOf(local))) {
                // Display slots follow the SDK local order (Parameter::id is
                // not stored for loaded chunks).
                const int slot = record && i < static_cast<int>(record->localParams.size()) ? i : -1;
                add({ElementKind::LocalParam, id, local->GetID(), slot}, id, "Local Parameter", NameOf(local),
                    NameOf(behavior), local->GetID());
            }
        }
    // Shortcuts: the graph's display records of other parameters (matched
    // by their source's CK ID).
    if ((kinds & SearchShortcuts) && record)
        for (int i = 0; i < static_cast<int>(record->sharedParams.size()); ++i) {
            const Parameter &shortcut = record->sharedParams[i];
            CKParameter *source = CKParameter::Cast(context->GetObject(shortcut.sourceId));
            if (source && matcher.Takes(source, true) && matcher.Matches(shortcut.sourceId, NameOf(source)))
                add({ElementKind::Shortcut, id, shortcut.id, i}, id, "Shortcut", NameOf(source), NameOf(behavior),
                    shortcut.sourceId);
        }
    if ((kinds & SearchOperations) && matcher.Unfiltered()) {
        CKParameterManager *manager = context->GetParameterManager();
        for (int i = 0; i < behavior->GetParameterOperationCount(); ++i) {
            CKParameterOperation *operation = behavior->GetParameterOperation(i);
            if (!operation) continue;
            const char *typeName = manager ? manager->OperationGuidToName(operation->GetOperationGuid()) : nullptr;
            const std::string type = typeName ? AnsiToUtf8(typeName) : std::string();
            if (matcher.Matches(operation->GetID(), NameOf(operation), type))
                add({ElementKind::Operation, id, operation->GetID()}, id, "Parameter Operation",
                    type.empty() ? NameOf(operation) : type, NameOf(behavior), operation->GetID());
        }
    }
    for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i)
        SearchBehavior(context, script, behavior->GetSubBehavior(i), data, matcher, kinds, results);
}

} // namespace

std::vector<SearchResult> SearchDocument(Document &document, const SearchQuery &query) {
    std::vector<SearchResult> results;
    CKContext *context = document.Context();
    if (!context || (!query.listAll && query.text.empty() && query.id == 0)) return results;
    const Matcher matcher(context, query);
    for (const auto &info : document.Scripts()) {
        if (query.onlyScripts && std::find(query.scripts.begin(), query.scripts.end(), info.id) == query.scripts.end())
            continue;
        ScriptSession *session = document.Session(info.id);
        const InterfaceData *data = session && !session->ReadOnly() ? &session->Data() : nullptr;
        SearchBehavior(context, info.id, CKBehavior::Cast(context->GetObject(info.id)), data, matcher,
                       query.kinds, results);
        if (!data || !(query.kinds & SearchComments) || (query.id && !query.listAll) || !matcher.Unfiltered()) continue;
        if (query.id) continue; // Comments have no CK ID
        auto searchComments = [&](const BehaviorData &record) {
            for (int i = 0; i < static_cast<int>(record.comments.size()); ++i) {
                const std::string text = AnsiToUtf8(record.comments[i].text);
                if (matcher.Matches(0, text)) {
                    SearchResult result{info.id, {ElementKind::Comment, record.id, record.comments[i].id, i},
                                        record.id, "Comment", text, AnsiToUtf8(info.name), 0};
                    results.push_back(result);
                }
            }
        };
        searchComments(data->rootBehavior);
        for (const auto &record : data->behaviors) searchComments(record);
    }
    return results;
}

EditResult ReplaceParameterValues(Document &document, const std::vector<std::pair<CK_ID, CK_ID>> &parameters,
                                  CK_ID valueId) {
    CKContext *context = document.Context();
    CKParameter *value = CKParameter::Cast(context->GetObject(valueId));
    if (!value) return EditResult::Refused("No value");
    const bool managed = ManagesMemory(context, value->GetType());
    const std::string text = ParameterText(value);
    const std::vector<unsigned char> bytes = ParameterBytes(value);
    Edit edit(document, "Replace Value");
    for (const auto &entry : parameters) {
        CKParameter *parameter = CKParameter::Cast(context->GetObject(entry.second));
        if (!parameter || parameter == value || parameter->GetType() != value->GetType()) continue;
        const EditResult set = managed ? SetParameterText(document, entry.first, entry.second, text)
                                       : SetParameterBytes(document, entry.first, entry.second, bytes);
        if (!set.ok && !set.Unchanged()) return edit.Fail(set.message);
    }
    if (edit.Empty()) return edit.Unchanged("The values are unchanged");
    return edit.Commit();
}

} // namespace vdev
