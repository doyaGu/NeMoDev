#include "application/EditorApplication.h"
#include "application/detail/EditorValidation.h"
#include "application/detail/EditorModules.h"
#include "application/detail/EditorConfiguration.h"
#include "application/DocumentDataCallbacks.h"
#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/runtime/TextEncoding.h"
#include "core/runtime/CkRuntimeHost.h"

#include <algorithm>
#include <array>
#include <deque>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace vdev {
using namespace detail;

// Native document-data registration stays beside the existing document adapter.
// Configuration owns the resulting registry without depending on native headers.
bool IsNativeDocumentDataType(const std::string &id) {
    return DefaultDocumentDataRegistry()->Find(id) != nullptr;
}
SelectedConfiguration::SelectedConfiguration() { m_Current.dataTypes = DefaultDocumentDataRegistry(); }
void SelectedConfiguration::Clear() {
    m_Current = {};
    m_Current.dataTypes = DefaultDocumentDataRegistry();
}

EditorDocumentData BorrowDocumentData(const DocumentDataRecord &record) {
    return {sizeof(EditorDocumentData), record.version, record.owner, record.bytes.data(), uint32_t(record.bytes.size()),
            {record.objects.data(), uint32_t(record.objects.size())}};
}
bool CopyDocumentData(const EditorDocumentData &value, DocumentDataRecord &record) {
    if (value.size != sizeof(value) || !value.version || value.byteCount > 16 * 1024 * 1024 ||
        value.objects.count > 65536 || (value.byteCount && !value.bytes) || (value.objects.count && !value.objects.data))
        return false;
    record.version = value.version;
    record.owner = value.owner;
    record.bytes.clear(); record.objects.clear();
    if (value.byteCount) record.bytes.assign(value.bytes, value.bytes + value.byteCount);
    if (value.objects.count) record.objects.assign(value.objects.data, value.objects.data + value.objects.count);
    return true;
}
void DocumentDataCallbacks::Close() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Closed = true;
}
bool DocumentDataCallbacks::Convert(const EditorDocumentDataDesc &desc, const DocumentDataRecord &first,
                                    const DocumentDataRecord *second, DocumentDataRecord &out, std::string &error) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Closed) { error = "Document data module has stopped"; return false; }
    struct Output {
        DocumentDataRecord record;
        uint32_t calls = 0;
        bool valid = false;
    } result;
    result.record.id = first.id;
    const EditorDocumentDataSink sink{&result, [](void *p, const EditorDocumentData *value) -> EditorStatus {
        auto &out = *static_cast<Output *>(p);
        if (++out.calls != 1 || !value) { out.valid = false; return EDITOR_INVALID; }
        try { out.valid = CopyDocumentData(*value, out.record); }
        catch (...) { out.valid = false; }
        return out.valid ? EDITOR_OK : EDITOR_INVALID;
    }};
    const auto a = BorrowDocumentData(first);
    EditorStatus status = EDITOR_ERROR;
    try {
        // The lock and output owner live outside this protected call. Windows
        // native faults must not jump over their cleanup to a file-task guard.
        const bool completed = GuardedCall([&] {
            if (second) {
                const auto b = BorrowDocumentData(*second);
                status = desc.merge(desc.data, &a, &b, sink);
            } else status = desc.upgrade(desc.data, &a, sink);
        }, nullptr, "Module document data");
        if (!completed) {
            error = "Module document data callback raised an exception: " + first.id;
            return false;
        }
    } catch (...) { error = "Module document data callback raised an exception: " + first.id; return false; }
    if (status != EDITOR_OK || result.calls != 1 || !result.valid) {
        error = "Module document data callback failed: " + first.id;
        return false;
    }
    out = std::move(result.record);
    return true;
}
DocumentDataType MakeDocumentDataType(const EditorDocumentDataDesc &desc,
                                     const std::shared_ptr<DocumentDataCallbacks> &callbacks) {
    DocumentDataType type;
    type.id.assign(desc.id.data, desc.id.size);
    type.version = desc.version;
    // Strings in desc belong to the registration. The worker needs only
    // callback pointers/data/version, never those borrowed string addresses.
    auto call = desc; call.id = {}; call.provider = {};
    if (call.upgrade) type.upgrade = [call, callbacks](const auto &old, auto &out, auto &error) {
        return callbacks->Convert(call, old, nullptr, out, error);
    };
    if (call.merge) type.merge = [call, callbacks](const auto &current, const auto &incoming, auto &out, auto &error) {
        return callbacks->Convert(call, current, &incoming, out, error);
    };
    return type;
}

namespace {
constexpr uint32_t Limit = 1024 * 1024;
constexpr uint64_t ArrayLimit = uint64_t(64) * Limit;
// Structure queries also describe every member. Their metadata adds one
// record level per native value level; leave room for a complete query result.
constexpr unsigned ValueDepth = 64;
struct ValueBudget {
    uint64_t text = 0, arrays = 0, nodes = 0;
    void Add(uint64_t words, uint64_t bytes) {
        text += words; arrays += bytes;
        if (text > Limit || arrays > ArrayLimit || ++nodes > 65536)
            throw std::invalid_argument("Value exceeds the size limit");
    }
    void Count(const std::vector<EditorField> &fields, unsigned depth = 0) {
        if (depth >= ValueDepth) throw std::invalid_argument("Value nesting is too deep");
        for (const auto &f : fields) {
            Add(f.key.size() + (f.type == EDITOR_TEXT_VALUE ? f.text.size() : 0),
                f.type == EDITOR_OBJECT_LIST ? f.objects.size() * sizeof(ObjectRef) :
                f.type == EDITOR_INTEGER_LIST ? f.integers.size() * sizeof(uint64_t) :
                f.type == EDITOR_REAL_LIST ? f.reals.size() * sizeof(double) :
                f.type == EDITOR_TEXT_LIST ? f.texts.size() * sizeof(EditorString) :
                f.type == EDITOR_RECORD ? f.fields.size() * sizeof(EditorValue) : 0);
            if (f.type == EDITOR_TEXT_LIST) for (const auto &text : f.texts) {
                this->text += text.size(); if (this->text > Limit) throw std::invalid_argument("Text exceeds the size limit");
            }
            if (f.type == EDITOR_RECORD && !f.fields.empty()) Count(f.fields, depth + 1);
        }
    }
    std::vector<EditorField> Copy(const EditorValue *values, uint32_t count, unsigned depth = 0) {
        Array(values, count);
        if (depth >= ValueDepth) throw std::invalid_argument("Value nesting is too deep");
        std::vector<EditorField> fields;
        fields.reserve(count);
        // Sort views of the copied keys, rather than allocating a tree node
        // and copying a key again for every value. Most records fit on stack.
        std::array<std::string_view, 64> smallKeys;
        std::vector<std::string_view> largeKeys;
        if (count > smallKeys.size()) largeKeys.resize(count);
        auto *keys = count > smallKeys.size() ? largeKeys.data() : smallKeys.data();
        for (uint32_t i = 0; i < count; ++i) {
            const auto &v = values[i];
            if (!ValueType(v.type)) throw std::invalid_argument("Unknown value type");
            uint64_t bytes = v.type == EDITOR_OBJECT_LIST ? uint64_t(v.objects.count) * sizeof(EditorObject) :
                v.type == EDITOR_INTEGER_LIST ? uint64_t(v.integers.count) * sizeof(uint64_t) :
                v.type == EDITOR_REAL_LIST ? uint64_t(v.reals.count) * sizeof(double) :
                v.type == EDITOR_TEXT_LIST ? uint64_t(v.texts.count) * sizeof(EditorString) :
                v.type == EDITOR_RECORD ? uint64_t(v.fields.count) * sizeof(EditorValue) : 0;
            Add(uint64_t(v.key.size) + (v.type == EDITOR_TEXT_VALUE ? v.text.size : 0), bytes);
            EditorField f;
            f.key = String(v.key); f.type = v.type;
            if (f.key.empty()) throw std::invalid_argument("Empty value key");
            if (v.type == EDITOR_OBJECT) f.object = v.object;
            if (v.type == EDITOR_TEXT_VALUE) f.text = String(v.text);
            if (v.type == EDITOR_INTEGER_VALUE) f.integer = v.integer;
            if (v.type == EDITOR_REAL_VALUE) {
                if (!std::isfinite(v.real)) throw std::invalid_argument("Non-finite value");
                f.real = v.real;
            }
            if ((v.type == EDITOR_OBJECT_LIST && v.objects.count && !v.objects.data) ||
                (v.type == EDITOR_INTEGER_LIST && v.integers.count && !v.integers.data) ||
                (v.type == EDITOR_TEXT_LIST && v.texts.count && !v.texts.data) ||
                (v.type == EDITOR_REAL_LIST && v.reals.count && !v.reals.data))
                throw std::invalid_argument("Missing value array");
            if (v.type == EDITOR_OBJECT_LIST && v.objects.count) f.objects.assign(v.objects.data, v.objects.data + v.objects.count);
            if (v.type == EDITOR_INTEGER_LIST && v.integers.count) f.integers.assign(v.integers.data, v.integers.data + v.integers.count);
            if (v.type == EDITOR_REAL_LIST) {
                for (uint32_t j = 0; j < v.reals.count; ++j)
                    if (!std::isfinite(v.reals.data[j])) throw std::invalid_argument("Non-finite component");
                if (v.reals.count) f.reals.assign(v.reals.data, v.reals.data + v.reals.count);
            }
            if (v.type == EDITOR_TEXT_LIST) for (uint32_t j = 0; j < v.texts.count; ++j) {
                text += v.texts.data[j].size;
                if (text > Limit) throw std::invalid_argument("Text exceeds the size limit");
                f.texts.push_back(String(v.texts.data[j]));
            }
            if (v.type == EDITOR_RECORD && v.fields.count) f.fields = Copy(v.fields.data, v.fields.count, depth + 1);
            fields.push_back(std::move(f));
            keys[i] = fields.back().key;
        }
        std::sort(keys, keys + count);
        if (std::adjacent_find(keys, keys + count) != keys + count)
            throw std::invalid_argument("Duplicate value key");
        return fields;
    }
};
EditorStatus EDITOR_CALL Collect(void *data, const EditorValue *values, uint32_t count) {
    try {
        auto &fields = *static_cast<std::vector<EditorField> *>(data);
        if (fields.size() + uint64_t(count) > 64) return EDITOR_INVALID;
        ValueBudget budget; budget.Count(fields);
        auto copied = budget.Copy(values, count);
        for (const auto &value : copied)
            if (std::any_of(fields.begin(), fields.end(), [&](const auto &f) { return f.key == value.key; })) return EDITOR_INVALID;
        fields.insert(fields.end(), std::make_move_iterator(copied.begin()), std::make_move_iterator(copied.end()));
        return EDITOR_OK;
    } catch (...) { return EDITOR_INVALID; }
}
struct Output {
    std::vector<EditorField> &fields;
    EditorStatus status = EDITOR_OK;
    static EditorStatus Write(void *data, const EditorValue *values, uint32_t count) {
        auto &output = *static_cast<Output *>(data);
        if (output.status != EDITOR_OK)
            return output.status;
        output.status = Collect(&output.fields, values, count);
        if (output.status != EDITOR_OK)
            output.fields.clear();
        return output.status;
    }
    EditorSink Sink() { return {this, Write}; }
};
EditorFieldValues BorrowResponse(const EditorResponse &response) {
    auto borrowed = BorrowEditorFields(response.fields);
    if (!response.message.empty())
        borrowed.values.push_back({EDITOR_TEXT("message"), EDITOR_TEXT_VALUE, {}, EditorText(response.message)});
    if (response.operation)
        borrowed.values.push_back({EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE, {}, {}, response.operation});
    return borrowed;
}
EditorStatus Deliver(const EditorResponse &response, const EditorFieldValues &borrowed, EditorSink sink, bool query) {
    if (sink.write) {
        try {
            if (sink.write(sink.data, borrowed.data(), static_cast<uint32_t>(borrowed.size())) != EDITOR_OK && query)
                return EDITOR_ERROR;
        } catch (...) {
            if (query)
                return EDITOR_ERROR;
        }
    }
    return response.status;
}
EditorStatus Send(const EditorResponse &response, EditorSink sink, bool query = true) {
    return sink.write ? Deliver(response, BorrowResponse(response), sink, query) : response.status;
}
} // namespace

bool operator==(const EditorField &a, const EditorField &b) {
    if (a.key != b.key || a.type != b.type) return false;
    switch (a.type) {
    case EDITOR_OBJECT: return a.object == b.object;
    case EDITOR_TEXT_VALUE: return a.text == b.text;
    case EDITOR_INTEGER_VALUE: return a.integer == b.integer;
    case EDITOR_REAL_VALUE: return a.real == b.real;
    case EDITOR_OBJECT_LIST: return a.objects == b.objects;
    case EDITOR_INTEGER_LIST: return a.integers == b.integers;
    case EDITOR_REAL_LIST: return a.reals == b.reals;
    case EDITOR_TEXT_LIST: return a.texts == b.texts;
    case EDITOR_RECORD: return a.fields == b.fields;
    }
    return false;
}
EditorFieldValues BorrowEditorFields(const std::vector<EditorField> &fields) {
    EditorFieldValues result;
    size_t texts = 0, members = 0;
    EveryField(fields, [&](const auto &field) {
        if (field.type == EDITOR_TEXT_LIST) texts += field.texts.size();
        if (field.type == EDITOR_RECORD) members += field.fields.size();
        return true;
    });
    result.texts.reserve(texts); result.members.reserve(members); result.values.reserve(fields.size());
    const auto append = [&](const auto &self, const std::vector<EditorField> &owned, std::vector<EditorValue> &values) -> size_t {
        const auto begin = values.size(); values.resize(begin + owned.size());
        for (size_t i = 0; i < owned.size(); ++i) {
            const auto &f = owned[i];
            EditorValue value{}; value.key = EditorText(f.key); value.type = f.type;
            if (f.type == EDITOR_OBJECT) value.object = f.object;
            if (f.type == EDITOR_TEXT_VALUE) value.text = EditorText(f.text);
            if (f.type == EDITOR_INTEGER_VALUE) value.integer = f.integer;
            if (f.type == EDITOR_REAL_VALUE) value.real = f.real;
            if (f.type == EDITOR_OBJECT_LIST) value.objects = {f.objects.data(), uint32_t(f.objects.size())};
            if (f.type == EDITOR_INTEGER_LIST) value.integers = {f.integers.data(), uint32_t(f.integers.size())};
            if (f.type == EDITOR_REAL_LIST) value.reals = {f.reals.data(), uint32_t(f.reals.size())};
            if (f.type == EDITOR_TEXT_LIST) {
                const auto offset = result.texts.size();
                for (const auto &text : f.texts) result.texts.push_back(EditorText(text));
                value.texts = {f.texts.empty() ? nullptr : result.texts.data() + offset, uint32_t(f.texts.size())};
            }
            if (f.type == EDITOR_RECORD && !f.fields.empty()) {
                const auto first = self(self, f.fields, result.members);
                value.fields = {result.members.data() + first, uint32_t(f.fields.size())};
            }
            values[begin + i] = value;
        }
        return begin;
    };
    append(append, fields, result.values);
    return result;
}

EditorStatus CopyEditorFields(std::vector<EditorField> &fields, const EditorValue *values, uint32_t count) {
    return Collect(&fields, values, count);
}

struct EditorApplication::CachedQuery {
    explicit CachedQuery(EditorResponse result) : response(std::move(result)), values(BorrowResponse(response)) {}
    const EditorResponse response;
    const EditorFieldValues values;
};

struct EditorApplication::State {
    std::function<Document *()> current;
    std::function<bool()> nativeAvailable;
    EditorHost host{};
    std::thread::id thread = std::this_thread::get_id();
    RegisteredModules modules;
    SelectedConfiguration configuration;
    const EditorValueEditors *drawingValues = nullptr;
    struct ToolState {
        std::string type, choice, fallback;
        const EditorToolDesc *handler = nullptr;
        bool entered = false, dispatching = false, captured = false, reset = false, focused = false;
    };
    std::map<EditorView, ToolState> toolStates;
    EditorView restoringView = 0, inputView = 0;
    bool active = false, available = false, writable = false, stopped = false;
    unsigned drawing = 0, querying = 0, executing = 0, editing = 0, drawingEdit = 0;
    unsigned readOnlyCallbacks = 0, checking = 0;
    DocumentToken document{};
    uint64_t revision = 0, contentRevision = 0, published = 1, next = 1;
    ObjectRef selection{};
    std::set<EditorView> scopes;
    EditorViewNavigation *navigation = nullptr;
    bool runningPending = false;
    struct Draft {
        EditorScope scope;
        ObjectRef object;
        PropertyBinding property;
        const EditorCommandDesc *command;
        const EditorValueEditorDesc *editor;
        EditorField value;
        std::vector<EditorField> fields;
        EditorValueEditors editors;
        std::vector<EditorField> arguments;
        void *preview = nullptr;
        bool commandOnly = false;
        bool toolOwned = false;
        bool deferred = false;
    };
    std::map<EditorEdit, Draft> drafts;
    struct Pending {
        const EditorCommandDesc *command;
        EditorScope scope;
        std::vector<EditorField> fields;
        EditorOperation operation = 0;
        std::shared_ptr<Draft> preview;
        EditorOperation afterEdit = 0;
        std::string revisionArgument;
        bool joinsEdit = false;
    };
    std::deque<Pending> pending;
    EditorOperation activeOperation = 0;
    struct Operation {
        EditorResponse result;
        bool documentEdit = false, asynchronous = false;
        std::string callbackError;
        EditorScope scope{};
    };
    std::map<EditorOperation, Operation> operations;
    std::deque<EditorOperation> completedOperations;
    bool Complete(EditorOperation operation, EditorResponse result) {
        const auto found = operations.find(operation);
        if (!operation || found == operations.end() || found->second.result.status != EDITOR_QUEUED ||
            result.status < EDITOR_OK || result.status >= EDITOR_QUEUED) return false;
        result.operation = operation;
        if (!found->second.callbackError.empty()) {
            if (!result.message.empty()) result.message += "; ";
            result.message += found->second.callbackError;
            result.status = EDITOR_ERROR;
        }
        found->second.result = std::move(result);
        completedOperations.push_back(operation);
        if (completedOperations.size() > 256) {
            operations.erase(completedOperations.front());
            completedOperations.pop_front();
        }
        return true;
    }
    struct Cached {
        EditorScope scope;
        std::vector<EditorField> arguments;
        std::shared_ptr<const CachedQuery> result;
        uint64_t version = 0;
    };
    std::map<const EditorQueryDesc *, std::vector<Cached>> cache;
    struct QueryRevision {
        const EditorQueryDesc *source = nullptr;
        uint64_t value = 0, published = 0;
    };
    std::map<std::string, QueryRevision> queryRevisions;
    EditorResponse queryResult;
    struct Watch {
        EditorScope scope;
        EditorChanged changed;
        void *data;
    };
    std::map<EditorSubscription, Watch> watches;
    uint64_t delivered = 0;
    bool notifying = false;
    State(std::function<Document *()> document, std::function<bool()> native)
        : current(std::move(document)), nativeAvailable(std::move(native)) {}
    bool NativeReady() const { return available && (!nativeAvailable || nativeAvailable()); }
    Document *CurrentDocument() const {
        try {
            return current ? current() : nullptr;
        } catch (...) {
            return nullptr;
        }
    }
    bool Current(ObjectRef object) const {
        auto *doc = CurrentDocument();
        return doc && doc->NativeId(object) != 0;
    }
    bool Thread() const { return thread == std::this_thread::get_id(); }
    bool Valid(EditorScope scope) const {
        return active && document.value && scope.document == document && (!scope.view || scopes.count(scope.view));
    }
    bool Valid(EditorScope scope, EditorArea area) const {
        if (area == EDITOR_DOCUMENT) return Valid(scope);
        return active && (!scope.view || scopes.count(scope.view)) &&
               (area == EDITOR_WORKSPACE || scope.document == document);
    }
    EditorStatus ResolveCommandView(const std::string &id, EditorScope &scope) const {
        if (!Valid(scope, EDITOR_WORKSPACE)) return EDITOR_STALE;
        const auto found = configuration.Get().bindingViews.find(id);
        if (found == configuration.Get().bindingViews.end() || found->second.empty() || !navigation) return EDITOR_OK;
        const auto view = navigation->ActionView(found->second, scope.view);
        if (!view) return EDITOR_REFUSED;
        // Routing selects a view, never a newer document for an old request.
        scope.view = view;
        return EDITOR_OK;
    }
    bool CurrentValues(const std::vector<EditorField> &fields) const {
        return EveryField(fields, [&](const auto &field) {
            if (field.type == EDITOR_OBJECT && field.object && !Current(field.object)) return false;
            return field.type != EDITOR_OBJECT_LIST || std::all_of(field.objects.begin(), field.objects.end(), [&](auto object) { return Current(object); });
        });
    }
    EditorStatus Arguments(const EditorParameter *parameters, uint32_t count, const EditorRequest &request,
                           std::vector<EditorField> &fields, EditorArea area = EDITOR_DOCUMENT,
                           bool liveReferences = true) const {
        if (!Thread() || !Valid(request.scope, area)) return EDITOR_STALE;
        if (request.count > count || Collect(&fields, request.values, request.count) != EDITOR_OK)
            return EDITOR_INVALID;
        for (const auto &field : fields) {
            bool found = false;
            for (uint32_t i = 0; i < count; ++i)
                found |= SameText(field.key, parameters[i].key) && field.type == parameters[i].type;
            if (!found) return EDITOR_INVALID;
        }
        for (uint32_t i = 0; i < count; ++i) {
            const auto &parameter = parameters[i];
            auto f = std::find_if(fields.begin(), fields.end(),
                                  [&](const EditorField &f) { return SameText(f.key, parameter.key); });
            if (f == fields.end()) {
                if (!parameter.optional) return EDITOR_INVALID;
                fields.push_back({String(parameter.key), parameter.type, {}, {}});
                continue;
            }
            if (f->type == EDITOR_OBJECT &&
                ((!f->object && !parameter.optional && !parameter.nullable) ||
                 (f->object && liveReferences &&
                  (f->object.document != document || (NativeReady() && !Current(f->object))))))
                return EDITOR_STALE;
        }
        if (liveReferences && !EveryField(fields, [&](const auto &field) {
                const auto valid = [&](auto object) {
                    return object.document == document && (!NativeReady() || Current(object));
                };
                if (field.type == EDITOR_OBJECT && field.object && !valid(field.object))
                    return false;
                return field.type != EDITOR_OBJECT_LIST ||
                       std::all_of(field.objects.begin(), field.objects.end(), valid);
            }))
            return EDITOR_STALE;
        return EDITOR_OK;
    }
    std::vector<EditorField> DraftFields(const Draft &draft, const EditorField &value) const {
        if (draft.commandOnly) {
            auto fields = draft.arguments;
            for (auto &field : fields)
                if (field.key == "value") {
                    field = value;
                    field.key = "value";
                }
            return fields;
        }
        auto owned = value;
        owned.key = draft.property.argument;
        std::vector<EditorField> fields{{"target", EDITOR_OBJECT, draft.object}, std::move(owned)};
        for (uint32_t i = 0; i < draft.command->count; ++i) {
            const auto key = String(draft.command->parameters[i].key);
            if (key == "target" || key == draft.property.argument)
                continue;
            const auto field =
                std::find_if(draft.fields.begin(), draft.fields.end(), [&](const auto &v) { return v.key == key; });
            if (field != draft.fields.end())
                fields.push_back(*field);
        }
        return fields;
    }
    bool ActiveDrafts() const {
        return std::any_of(drafts.begin(), drafts.end(),
                           [&](const auto &entry) { return !runningPending || !entry.second.deferred; });
    }
    EditorStatus RefreshDraft(Draft &draft) {
        if (!Valid(draft.scope) || !Current(draft.object))
            return EDITOR_STALE;
        const auto arguments = BorrowEditorFields(draft.arguments);
        const EditorRequest request{draft.scope, arguments.data(), uint32_t(arguments.size()),
                                    EditorText(draft.property.queryProvider)};
        std::vector<EditorField> fields;
        const EditorSink sink{&fields, [](void *data, const EditorValue *values, uint32_t count) {
                                  std::vector<EditorField> copied;
                                  const auto result = CopyEditorFields(copied, values, count);
                                  if (result == EDITOR_OK) {
                                      auto &output = *static_cast<std::vector<EditorField> *>(data);
                                      output.insert(output.end(), std::make_move_iterator(copied.begin()),
                                                    std::make_move_iterator(copied.end()));
                                  }
                                  return result;
                              }};
        const auto status = host.query(host.data, EditorText(draft.property.query), &request, sink);
        if (status != EDITOR_OK)
            return status;
        if (std::none_of(fields.begin(), fields.end(), [&](const auto &field) {
                return field.key == draft.property.field && field.type == draft.property.type;
            }))
            return EDITOR_INVALID;
        draft.fields = std::move(fields);
        draft.deferred = false;
        return EDITOR_OK;
    }
    EditorStatus Preview(Draft &draft, const EditorField &value, EditorPreviewAction action) {
        if (draft.deferred || !draft.command->preview || (action == EDITOR_PREVIEW_CANCEL && !draft.preview))
            return EDITOR_OK;
        if (action == EDITOR_PREVIEW_UPDATE && !NativeReady())
            return EDITOR_REFUSED;
        const auto fields = DraftFields(draft, value);
        const auto values = BorrowEditorFields(fields);
        if (action == EDITOR_PREVIEW_UPDATE) {
            const auto allowed = Check(*draft.command, draft.scope, fields, true);
            if (allowed != EDITOR_OK)
                return allowed;
        }
        const EditorRequest request{draft.scope, values.data(), uint32_t(values.values.size())};
        ++executing;
        ++readOnlyCallbacks;
        EditorStatus status = EDITOR_ERROR;
        try {
            status = draft.command->preview(draft.command->data, &host, &request, action, &draft.preview);
        } catch (...) {
        }
        --readOnlyCallbacks;
        --executing;
        cache.clear();
        ++published;
        return status == EDITOR_OK || status == EDITOR_STALE ? status : EDITOR_ERROR;
    }
    bool RestorePreview(Draft &draft) {
        const auto status = Preview(draft, draft.value, EDITOR_PREVIEW_CANCEL);
        const bool restored = (status == EDITOR_OK || status == EDITOR_STALE) && !draft.preview;
        if (!restored)
            if (auto *doc = CurrentDocument(); doc && doc->Token() == draft.scope.document)
                doc->InvalidateHistory();
        return restored;
    }
    void ClearDrafts() {
        for (auto &entry : drafts)
            RestorePreview(entry.second);
        drafts.clear();
    }
    EditorStatus Check(const EditorCommandDesc &command, EditorScope scope, const std::vector<EditorField> &fields, bool fromDraft = false) {
        if (!Thread() || !Valid(scope, command.area)) return EDITOR_STALE;
        if (readOnlyCallbacks || modules.Stopped()) return EDITOR_REFUSED;
        const bool native = command.effect != EDITOR_SESSION_CHANGE;
        // Window navigation can close the owner of a draft. Document session
        // changes, including selection, still wait for the draft to finish.
        const bool navigation = !native && command.area == EDITOR_WORKSPACE;
        if ((native && !NativeReady()) || (!fromDraft && ActiveDrafts() && !navigation))
            return EDITOR_REFUSED;
        if (!NativeReady() && !EveryField(fields, [](const auto &field) {
            return !(field.type == EDITOR_OBJECT && field.object) && !(field.type == EDITOR_OBJECT_LIST && !field.objects.empty());
        })) return EDITOR_REFUSED;
        if ((command.effect == EDITOR_DOCUMENT_EDIT || command.effect == EDITOR_DOCUMENT_CHANGE ||
             command.effect == EDITOR_HISTORY_CHANGE) && !writable)
            return EDITOR_REFUSED;
        auto *doc = NativeReady() ? CurrentDocument() : nullptr;
        if (command.area == EDITOR_DOCUMENT && native && (!doc || doc->Token() != scope.document))
            return EDITOR_STALE;
        if ((editing && command.effect != EDITOR_DOCUMENT_EDIT) ||
            (doc && !editing && doc->EditOpen() && !navigation) || executing >= 16)
            return EDITOR_REFUSED;
        if (!command.state) return EDITOR_OK;
        if (querying >= 16) return EDITOR_REFUSED;
        const auto values = BorrowEditorFields(fields);
        const EditorRequest request{scope, values.data(), static_cast<uint32_t>(values.size())};
        ++querying; ++checking;
        EditorStatus status;
        try { status = command.state(command.data, &host, &request); }
        catch (...) { status = EDITOR_ERROR; }
        --checking; --querying;
        return status >= EDITOR_OK && status <= EDITOR_ERROR ? status : EDITOR_ERROR;
    }
    void RefreshDocument() {
        if (querying || executing || notifying || !NativeReady()) return;
        auto *doc = CurrentDocument();
        if (!doc || doc->Token() != document || doc->EditOpen()) return;
        if (contentRevision == doc->ChangeRevision() && revision == doc->TopologyRevision()) return;
        contentRevision = doc->ChangeRevision();
        revision = doc->TopologyRevision();
        cache.clear();
        ++published;
    }
    EditorResponse Run(const EditorCommandDesc &command, EditorScope scope, const std::vector<EditorField> &fields,
                       EditorOperation operation = 0, Draft *preview = nullptr) {
        EditorResponse result;
        if (!operation) {
            if (activeOperation) operation = activeOperation;
            else {
                if (operations.size() >= completedOperations.size() + 256) { result.status = EDITOR_REFUSED; return result; }
                operation = next++;
                operations.emplace(operation, Operation{EditorResponse{EDITOR_QUEUED}, command.effect == EDITOR_DOCUMENT_EDIT});
                operations.at(operation).scope = scope;
            }
        }
        result.operation = operation;
        result.status = (readOnlyCallbacks || checking) ? EDITOR_REFUSED : Check(command, scope, fields);
        if (result.status != EDITOR_OK) { if (!executing) Complete(operation, result); return result; }
        if (!CurrentValues(fields)) {
            result.status = EDITOR_STALE;
            if (!executing) Complete(operation, result);
            return result;
        }
        auto *doc = NativeReady() ? CurrentDocument() : nullptr;
        auto values = BorrowEditorFields(fields);
        EditorRequest request{scope, values.data(), static_cast<uint32_t>(values.size()), {}, operation};
        struct Active {
            EditorOperation &value, previous;
            ~Active() { value = previous; }
        } restore{activeOperation, activeOperation};
        activeOperation = operation;
        // Nested editor commands and the existing core commands join this
        // edit. Only the outer command adds a history step or rolls it back.
        std::unique_ptr<Edit> edit;
        if (command.effect == EDITOR_DOCUMENT_EDIT && !editing)
            edit = std::make_unique<Edit>(*doc, Utf8ToAnsi(String(command.title)), Edit::Marks::Nested);
        if (command.effect == EDITOR_DOCUMENT_EDIT) ++editing;
        ++executing;
        Output output{result.fields};
        try {
            result.status = preview && preview->preview && command.commitPreview
                                ? command.commitPreview(command.data, &host, &request, preview->preview, output.Sink())
                                : command.run(command.data, &host, &request, output.Sink());
            auto message = std::find_if(result.fields.begin(), result.fields.end(), [](const EditorField &field) {
                return field.key == "message" && field.type == EDITOR_TEXT_VALUE;
            });
            if (message != result.fields.end()) {
                result.message = message->text;
                result.fields.erase(message);
            }
        } catch (...) {
            result.status = EDITOR_ERROR;
            result.message = "Command callback raised an exception";
        }
        --executing;
        if (command.effect == EDITOR_DOCUMENT_EDIT) --editing;
        if (output.status != EDITOR_OK)
            result.status = output.status;
        if (result.status < EDITOR_OK || result.status > EDITOR_QUEUED ||
            (command.effect == EDITOR_DOCUMENT_EDIT && result.status == EDITOR_QUEUED))
            result.status = EDITOR_ERROR;
        cache.clear();
        if (preview && preview->preview && (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE)) {
            const auto released = Preview(*preview, preview->value, EDITOR_PREVIEW_RELEASE);
            if ((released != EDITOR_OK && released != EDITOR_STALE) || preview->preview) {
                result.status = EDITOR_ERROR;
                result.message = "Cannot finish the command preview";
            }
        }
        if (edit) {
            if (result.status == EDITOR_OK) {
                const auto committed = edit->Commit();
                if (!committed.ok) {
                    result.status = committed.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_ERROR;
                    if (!committed.message.empty() && (!committed.Unchanged() || result.message.empty()))
                        result.message = AnsiToUtf8(committed.message);
                }
            } else if (!edit->Cancel()) {
                result.status = EDITOR_ERROR;
                result.message = "Command failed and could not be undone";
            }
        }
        if (preview && preview->preview && !RestorePreview(*preview)) {
            result.status = EDITOR_ERROR;
            result.message = "Cannot restore the command preview";
        }
        if (doc && command.effect == EDITOR_DOCUMENT_EDIT)
            result.documentRevision = doc->ChangeRevision();
        RefreshDocument();
        const auto record = operations.find(operation);
        if (record != operations.end() && result.status == EDITOR_QUEUED) record->second.asynchronous = true;
        if ((!executing || restore.previous != operation) && result.status != EDITOR_QUEUED) {
            if (record != operations.end() && record->second.asynchronous) {
                // A nested provider has already handed this operation to an
                // asynchronous owner. Keep its actual outputs and cleanup
                // alive even if an outer callback fails after accepting work.
                auto &owned = record->second;
                if (result.status != EDITOR_OK && result.status != EDITOR_NO_CHANGE) {
                    owned.callbackError = result.message.empty() ? "Command failed after starting asynchronous work" : result.message;
                    if (owned.result.status != EDITOR_QUEUED) {
                        owned.result.status = EDITOR_ERROR;
                        if (!owned.result.message.empty()) owned.result.message += "; ";
                        owned.result.message += owned.callbackError;
                    }
                }
                if (owned.result.status == EDITOR_QUEUED) result.status = EDITOR_QUEUED;
            } else Complete(operation, result);
        }
        return result;
    }
};

EditorApplication::EditorApplication(std::function<Document *()> current, std::function<bool()> nativeAvailable)
    : m_State(std::make_unique<State>(std::move(current), std::move(nativeAvailable))) {
    auto &h = m_State->host;
    h.size = sizeof(h);
    h.version = EDITOR_VERSION;
    h.data = this;
    h.readDocumentData = [](void *p, EditorScope scope, EditorString id, ObjectRef owner,
                            EditorDocumentDataSink sink) -> EditorStatus {
        auto &s = *static_cast<EditorApplication *>(p)->m_State;
        if (!s.Thread() || !s.NativeReady() || !s.Valid(scope)) return EDITOR_STALE;
        if (!sink.write || s.querying >= 16) return EDITOR_INVALID;
        auto *doc = s.CurrentDocument();
        if (!doc || (owner && !doc->Resolve(owner))) return EDITOR_STALE;
        try {
            const auto *record = FindDocumentData(*doc, Id(id), owner);
            if (!record) return EDITOR_NO_CHANGE;
            // A sink may call back into the editor. Own its borrowed data until
            // delivery ends, and prohibit edits during that read-only callback.
            const auto copy = *record;
            const auto value = BorrowDocumentData(copy);
            struct Guard {
                State &state;
                explicit Guard(State &state) : state(state) { ++state.readOnlyCallbacks; ++state.querying; }
                ~Guard() { --state.querying; --state.readOnlyCallbacks; }
            } guard(s);
            return sink.write(sink.data, &value);
        } catch (...) { return EDITOR_ERROR; }
    };
    h.writeDocumentData = [](void *p, EditorScope scope, EditorString id, ObjectRef owner,
                             const EditorDocumentData *value) -> EditorStatus {
        auto &s = *static_cast<EditorApplication *>(p)->m_State;
        if (!s.Thread() || !s.NativeReady() || !s.Valid(scope)) return EDITOR_STALE;
        auto *doc = s.CurrentDocument();
        if (!s.executing || !s.editing || s.drawing || s.querying || s.checking || s.readOnlyCallbacks ||
            !doc || !doc->EditOpen()) return EDITOR_REFUSED;
        try {
            const auto name = Id(id);
            if (!s.configuration.Get().documentData.count(name)) return EDITOR_REFUSED;
            if (value && value->owner != owner) return EDITOR_INVALID;
            if (!value) {
                const auto result = RemoveDocumentData(*doc, name, owner);
                return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
            }
            DocumentDataRecord record; record.id = name;
            if (!CopyDocumentData(*value, record)) return EDITOR_INVALID;
            const auto result = SetDocumentData(*doc, std::move(record));
            return result.ok ? EDITOR_OK : result.Unchanged() ? EDITOR_NO_CHANGE : EDITOR_REFUSED;
        } catch (...) { return EDITOR_ERROR; }
    };
    h.query = [](void *p, EditorString id, const EditorRequest *r, EditorSink sink) -> EditorStatus {
        try {
            return r ? static_cast<EditorApplication *>(p)->DeliverQuery(Identifier(id), *r, sink) : EDITOR_INVALID;
        } catch (...) {
            return EDITOR_INVALID;
        }
    };
    h.commandState = [](void *p, EditorString id, const EditorRequest *request) -> EditorStatus {
        if (!request)
            return EDITOR_INVALID;
        try {
            return static_cast<EditorApplication *>(p)->CommandState(Id(id), *request);
        } catch (...) {
            return EDITOR_INVALID;
        }
    };
    h.canEdit = [](void *p, EditorString id, EditorScope scope, EditorObject target) -> EditorStatus {
        try {
            return static_cast<EditorApplication *>(p)->CanEdit(Id(id), scope, target);
        } catch (...) {
            return EDITOR_INVALID;
        }
    };
    h.execute = [](void *p, EditorString id, const EditorRequest *r, EditorSink sink) -> EditorStatus {
        try {
            if (!r) return EDITOR_INVALID;
            auto &app = *static_cast<EditorApplication *>(p);
            auto result = app.Execute(Id(id), *r);
            if (app.m_State->executing && result.operation == app.m_State->activeOperation) result.operation = 0;
            return Send(result, sink, false);
        } catch (...) {
            return EDITOR_INVALID;
        }
    };
    h.beginEdit = [](void *p, EditorString property, const EditorRequest *request,
                     EditorEdit *out) -> EditorStatus {
        if (!out)
            return EDITOR_INVALID;
        *out = 0;
        if (!request) return EDITOR_INVALID;
        try {
            return static_cast<EditorApplication *>(p)->BeginEdit(Id(property), *request, *out);
        } catch (...) {
            return EDITOR_INVALID;
        }
    };
    h.drawProperty = [](void *p, EditorString property, const EditorRequest *request, EditorEdit *edit, EditorString label) {
        if (!edit || !request) return uint32_t(EDITOR_INPUT_INVALID);
        try { return static_cast<EditorApplication *>(p)->DrawProperty(Id(property), *request, *edit, label); }
        catch (...) { return uint32_t(EDITOR_INPUT_CANCEL); }
    };
    h.drawValue = [](void *p, EditorString editor, const EditorValueInput *input) {
        if (!input || input->size != sizeof(*input) || !static_cast<EditorApplication *>(p)->m_State->drawing) return uint32_t(EDITOR_INPUT_INVALID);
        try { return static_cast<EditorApplication *>(p)->DrawValue(Id(editor), *input); }
        catch (...) { return uint32_t(EDITOR_INPUT_INVALID); }
    };
    h.editValue = [](void *p, EditorEdit edit, const EditorValue *value) -> EditorStatus {
        try { return value ? static_cast<EditorApplication *>(p)->UpdateEdit(edit, *value) : EDITOR_INVALID; }
        catch (...) { return EDITOR_INVALID; }
    };
    h.finishEdit = [](void *p, EditorEdit edit, uint32_t commit, EditorOperation *operation) -> EditorStatus {
        try {
            if (operation)
                *operation = 0;
            if (commit > 1)
                return EDITOR_INVALID;
            return static_cast<EditorApplication *>(p)->FinishEdit(edit, commit != 0, operation);
        } catch (...) {
            return EDITOR_ERROR;
        }
    };
    h.drawEdit = [](void *p, EditorEdit edit, EditorString label, uint32_t focus) {
        try {
            return static_cast<EditorApplication *>(p)->DrawEdit(edit, label, focus);
        } catch (...) {
            return uint32_t(EDITOR_INPUT_CANCEL);
        }
    };
    h.watch = [](void *p, EditorView view, EditorChanged changed, void *data, EditorSubscription *out) -> EditorStatus {
        if (!out)
            return EDITOR_INVALID;
        *out = 0;
        auto &self = *static_cast<EditorApplication *>(p);
        auto &s = *self.m_State;
        if (!s.Thread() || !changed || !view || !s.active || !s.scopes.count(view))
            return EDITOR_STALE;
        if (s.checking) return EDITOR_REFUSED;
        try {
            const auto id = s.next++;
            s.watches.emplace(id, State::Watch{self.Scope(view), changed, data});
            *out = id;
            return EDITOR_OK;
        } catch (...) {
            return EDITOR_ERROR;
        }
    };
    h.unwatch = [](void *p, EditorSubscription id) {
        auto &s = *static_cast<EditorApplication *>(p)->m_State;
        if (s.Thread() && !s.checking)
            s.watches.erase(id);
    };
    h.scope = [](void *p, EditorView view) { return static_cast<EditorApplication *>(p)->Scope(view); };
    h.selectTool = [](void *p, EditorScope scope, EditorString id) {
        try {
            return static_cast<EditorApplication *>(p)->SelectTool(scope, Id(id));
        } catch (...) {
            return EDITOR_ERROR;
        }
    };
    h.activeTool = [](void *p, EditorView view) {
        auto *tool = static_cast<EditorApplication *>(p)->ActiveTool(view);
        return tool ? tool->id : EditorString{};
    };
    h.toolInput = [](void *p, EditorScope scope, const EditorToolInput *input) {
        if (!input) return EDITOR_INVALID;
        return static_cast<EditorApplication *>(p)->ToolInput(scope, *input);
    };
    h.cancelTool = [](void *p, EditorView view) { static_cast<EditorApplication *>(p)->CancelTool(view); };
    h.toolbar = [](void *p, EditorString location, EditorScope scope, const EditorToolbarFrame *frame) {
        auto &self = *static_cast<EditorApplication *>(p);
        auto &s = *self.m_State;
        if (!frame || frame->size != sizeof(*frame) || !s.Thread() || !s.active || !s.drawing || s.querying ||
            s.readOnlyCallbacks || !s.navigation)
            return EDITOR_REFUSED;
        if (!s.Valid(scope, EDITOR_WORKSPACE)) return EDITOR_STALE;
        try {
            return s.navigation->Toolbar(Id(location), scope, *frame);
        } catch (...) {
            return EDITOR_ERROR;
        }
    };
    h.shortcuts = [](void *p, EditorScope scope) {
        auto &s = *static_cast<EditorApplication *>(p)->m_State;
        if (s.Thread() && s.active && s.drawing && !s.querying && !s.readOnlyCallbacks && s.navigation &&
            s.Valid(scope, EDITOR_WORKSPACE))
            s.navigation->Shortcuts(scope);
    };
    h.inputBinding = [](void *p, EditorScope scope, EditorString action, EditorInputPhase phase,
                        uint32_t consume) -> uint32_t {
        auto &s = *static_cast<EditorApplication *>(p)->m_State;
        if (!s.Thread() || !s.active || !s.drawing || s.querying || s.checking || s.readOnlyCallbacks ||
            !s.navigation || !s.Valid(scope, EDITOR_WORKSPACE) || phase < EDITOR_KEY_PRESSED ||
            phase > EDITOR_KEY_RELEASED || consume > 1)
            return 0;
        try {
            return s.navigation->InputBinding(scope, Id(action), phase, consume != 0);
        } catch (...) {
            return 0;
        }
    };
    h.revision = [](void *p) { return static_cast<EditorApplication *>(p)->Revision(); };
    h.selection = [](void *p) { return static_cast<EditorApplication *>(p)->Selection(); };
    h.completeOperation = [](void *p, EditorOperation operation, EditorStatus status,
                             const EditorValue *values, uint32_t count) -> EditorStatus {
        EditorResponse result;
        result.status = status;
        if (Collect(&result.fields, values, count) != EDITOR_OK) return EDITOR_INVALID;
        return static_cast<EditorApplication *>(p)->CompleteOperation(operation, std::move(result)) ? EDITOR_OK : EDITOR_STALE;
    };
    h.navigateView = [](void *p, EditorString type, EditorView instance, const EditorRequest *request,
                         EditorSink sink) -> EditorStatus {
        auto &app = *static_cast<EditorApplication *>(p);
        auto &s = *app.m_State;
        if (!s.Thread() || !s.active || !s.navigation || !s.executing || s.editing || s.drawing || s.querying || s.notifying)
            return EDITOR_REFUSED;
        if (!request) return EDITOR_INVALID;
        try {
            const auto found = s.configuration.Get().views.find(Id(type));
            if (found == s.configuration.Get().views.end()) return EDITOR_INVALID;
            const auto &desc = *found->second;
            if (desc.area == EDITOR_DOCUMENT && !s.NativeReady()) return EDITOR_REFUSED;
            std::vector<EditorField> fields;
            const auto status = s.Arguments(desc.parameters, desc.count, *request, fields, desc.area);
            if (status != EDITOR_OK) return status;
            const auto values = BorrowEditorFields(fields);
            const EditorRequest validated{request->scope, values.data(), uint32_t(values.size()), {}, request->operation};
            return s.navigation->Navigate(desc, instance, validated, sink);
        } catch (...) { return EDITOR_ERROR; }
    };
    h.queryView = [](void *p, EditorView view, EditorSink sink) -> EditorStatus {
        auto &s = *static_cast<EditorApplication *>(p)->m_State;
        if (!s.Thread() || !s.active || !s.navigation || !s.scopes.count(view)) return EDITOR_STALE;
        if(s.readOnlyCallbacks||s.querying>=16)return EDITOR_REFUSED;
        try { return s.navigation->Inspect(view, sink); } catch (...) { return EDITOR_ERROR; }
    };
    h.pickRegions = [](void *p, EditorView view, const EditorPickRegion *regions, uint32_t count) -> EditorStatus {
        auto &s = *static_cast<EditorApplication *>(p)->m_State;
        if (!s.Thread() || !s.active || !s.navigation || !s.scopes.count(view)) return EDITOR_STALE;
        if(s.readOnlyCallbacks||s.querying||!s.drawing)return EDITOR_REFUSED;
        if (count > 65536 || (count && !regions)) return EDITOR_INVALID;
        for (uint32_t i = 0; i < count; ++i)
            if (!regions[i].object || regions[i].object.document != s.document ||
                !std::isfinite(regions[i].left) || !std::isfinite(regions[i].top) ||
                !std::isfinite(regions[i].right) || !std::isfinite(regions[i].bottom) ||
                regions[i].left > regions[i].right || regions[i].top > regions[i].bottom) return EDITOR_INVALID;
        try { return s.navigation->PickRegions(view, regions, count); } catch (...) { return EDITOR_ERROR; }
    };
    h.queryRevision = [](void *p, EditorString id, uint64_t *out) -> EditorStatus {
        if (!out) return EDITOR_INVALID;
        try { return static_cast<EditorApplication *>(p)->QueryRevision(Id(id), *out); }
        catch (...) { return EDITOR_ERROR; }
    };
    h.shortcut = [](void *p, EditorString action) -> uint32_t {
        auto &s=*static_cast<EditorApplication*>(p)->m_State;
        if (!s.Thread() || !s.active || !s.navigation || !s.drawing || s.querying || s.readOnlyCallbacks) return 0;
        try { return s.navigation->Shortcut(Id(action), true); } catch (...) { return 0; }
    };
    h.beginCommand = [](void *p, EditorString id, const EditorRequest *request, EditorEdit *edit) -> EditorStatus {
        if (!request || !edit)
            return EDITOR_INVALID;
        try {
            return static_cast<EditorApplication *>(p)->BeginCommand(Id(id), *request, *edit);
        } catch (...) {
            return EDITOR_ERROR;
        }
    };
    h.previewCommand = [](void *p, EditorString id, const EditorRequest *request, EditorPreviewAction action,
                          void **state, EditorSink sink) -> EditorStatus {
        try {
            auto &s = *static_cast<EditorApplication *>(p)->m_State;
            if (action < EDITOR_PREVIEW_UPDATE || action > EDITOR_PREVIEW_COMMIT)
                return EDITOR_INVALID;
            if (!request || !state || !s.Thread() || !s.executing || s.executing >= 16 || s.querying || s.checking ||
                (action == EDITOR_PREVIEW_COMMIT ? !s.editing : !s.readOnlyCallbacks))
                return EDITOR_REFUSED;
            const auto selected = s.configuration.Get().commands.find(Id(id));
            const EditorCommandDesc *command = selected == s.configuration.Get().commands.end() ? nullptr : selected->second;
            if (request->handler.size) {
                command = nullptr;
                for (const auto &module : s.modules.All())
                    for (const auto &candidate : module->commands)
                        if (String(candidate.id) == Id(id) && String(candidate.provider) == String(request->handler))
                            command = &candidate;
            }
            if (!command || !command->preview || !command->commitPreview)
                return EDITOR_INVALID;
            std::vector<EditorField> fields;
            EditorStatus status = EDITOR_OK;
            if (action == EDITOR_PREVIEW_UPDATE || action == EDITOR_PREVIEW_COMMIT) {
                if (!s.NativeReady())
                    return EDITOR_REFUSED;
                status = s.Arguments(command->parameters, command->count, *request, fields, command->area);
            } else {
                // Scope closure and document replacement still have to free the
                // previous provider's state, even after its objects have expired.
                status = CopyEditorFields(fields, request->values, request->count);
            }
            if (status != EDITOR_OK)
                return status;
            const auto values = BorrowEditorFields(fields);
            const EditorRequest validated{request->scope, values.data(), uint32_t(values.size()), request->handler,
                                          request->operation};
            ++s.executing;
            EditorStatus result = EDITOR_ERROR;
            try {
                result = action == EDITOR_PREVIEW_COMMIT
                             ? command->commitPreview(command->data, &s.host, &validated, *state, sink)
                             : command->preview(command->data, &s.host, &validated, action, state);
            } catch (...) {
            }
            --s.executing;
            return result;
        } catch (...) {
            return EDITOR_ERROR;
        }
    };
    h.menu = [](void *p, EditorString location, EditorScope scope, ObjectRef target, EditorEdit *edit) -> EditorStatus {
        auto &s = *static_cast<EditorApplication *>(p)->m_State;
        if (!edit || !s.Thread() || !s.active || !s.navigation || !s.drawing || s.querying || s.readOnlyCallbacks)
            return EDITOR_REFUSED;
        if (!s.scopes.count(scope.view) || scope.document != s.document)
            return EDITOR_STALE;
        try {
            return s.navigation->Menu(Id(location), scope, target, *edit);
        } catch (...) {
            return EDITOR_ERROR;
        }
    };
    const EditorParameter argument{EDITOR_TEXT("operation"), EDITOR_INTEGER_VALUE};
    const EditorParameter fields[] = {
        {EDITOR_TEXT("status"), EDITOR_INTEGER_VALUE},  {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("written"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("message"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE},   {EDITOR_TEXT("document-revision"), EDITOR_INTEGER_VALUE}};
    EditorQueryDesc query{sizeof(query),
                          EDITOR_TEXT("application.operation"),
                          EDITOR_TEXT("builtin.application"),
                          &argument,
                          1,
                          nullptr,
                          m_State.get(),
                          EDITOR_QUERY_EACH_READ,
                          EDITOR_APPLICATION,
                          fields,
                          6};
    query.read = [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) -> EditorStatus {
        auto &s = *static_cast<State *>(data);
        const auto found = s.operations.find(request->values[0].integer);
        if (found == s.operations.end()) return EDITOR_STALE;
        const auto &result = found->second.result;
        std::string path, message = result.message;
        uint64_t written = 0;
        for (const auto &field : result.fields) {
            if (field.key == "path" && field.type == EDITOR_TEXT_VALUE) path = field.text;
            if (field.key == "written" && field.type == EDITOR_INTEGER_VALUE) written = field.integer;
            if (field.key == "message" && field.type == EDITOR_TEXT_VALUE) message = field.text;
        }
        const EditorValue values[] = {
            {EditorLiteral("status"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(result.status)},
            {EditorLiteral("path"), EDITOR_TEXT_VALUE, {}, EditorText(path)},
            {EditorLiteral("written"), EDITOR_INTEGER_VALUE, {}, {}, written},
            {EditorLiteral("message"), EDITOR_TEXT_VALUE, {}, EditorText(message)},
            {EditorLiteral("count"), EDITOR_INTEGER_VALUE, {}, {}, result.fields.size()},
            {EditorLiteral("document-revision"), EDITOR_INTEGER_VALUE, {}, {}, result.documentRevision}};
        return sink.write ? sink.write(sink.data, values, 6) : EDITOR_INVALID;
    };
    const EditorParameter outputArgs[] = {argument, {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE}};
    const EditorParameter outputFields[] = {{EDITOR_TEXT("key"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("object"), EDITOR_OBJECT},
        {EDITOR_TEXT("text"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("integer"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("integers"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("texts"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("real"), EDITOR_REAL_VALUE},
        {EDITOR_TEXT("reals"), EDITOR_REAL_LIST}, {EDITOR_TEXT("fields"), EDITOR_RECORD}};
    EditorQueryDesc output{sizeof(output), EDITOR_TEXT("application.operation-output"), EDITOR_TEXT("builtin.application"),
        outputArgs, 2, nullptr, m_State.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, outputFields, 11};
    output.read = [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) -> EditorStatus {
        const auto &state = *static_cast<State *>(data);
        uint64_t operation = 0, index = 0;
        for (uint32_t i = 0; i < request->count; ++i)
            if (String(request->values[i].key) == "operation") operation = request->values[i].integer;
            else if (String(request->values[i].key) == "index") index = request->values[i].integer;
        const auto found = state.operations.find(operation);
        if (found == state.operations.end()) return EDITOR_STALE;
        if (found->second.result.status == EDITOR_QUEUED) return EDITOR_REFUSED;
        if (index >= found->second.result.fields.size()) return EDITOR_INVALID;
        const auto &field = found->second.result.fields[static_cast<size_t>(index)];
        std::vector<EditorString> texts;
        for (const auto &text : field.texts) texts.push_back(EditorText(text));
        const auto nested = BorrowEditorFields(field.fields);
        EditorValue record{}; record.key = EDITOR_TEXT("fields"); record.type = EDITOR_RECORD;
        record.fields = {nested.data(), uint32_t(nested.size())};
        const EditorValue values[] = {
            {EDITOR_TEXT("key"), EDITOR_TEXT_VALUE, {}, EditorText(field.key)},
            {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(field.type)},
            {EDITOR_TEXT("object"), EDITOR_OBJECT, field.object},
            {EDITOR_TEXT("text"), EDITOR_TEXT_VALUE, {}, EditorText(field.text)},
            {EDITOR_TEXT("integer"), EDITOR_INTEGER_VALUE, {}, {}, field.integer},
            {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, {}, {}, 0,
             {field.objects.data(), static_cast<uint32_t>(field.objects.size())}},
            {EDITOR_TEXT("integers"), EDITOR_INTEGER_LIST, {}, {}, 0, {},
             {field.integers.data(), static_cast<uint32_t>(field.integers.size())}},
            {EDITOR_TEXT("texts"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {},
             {texts.data(), static_cast<uint32_t>(texts.size())}},
            {EDITOR_TEXT("real"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, field.real},
            {EDITOR_TEXT("reals"), EDITOR_REAL_LIST, {}, {}, 0, {}, {}, {}, 0,
             {field.reals.data(), static_cast<uint32_t>(field.reals.size())}}, record};
        return sink.write ? sink.write(sink.data, values, 11) : EDITOR_INVALID;
    };
    const EditorParameter themeArgument{EDITOR_TEXT("theme"), EDITOR_TEXT_VALUE, 1};
    const EditorParameter themeField{EDITOR_TEXT("values"), EDITOR_RECORD};
    const EditorQueryDesc appearance{
        sizeof(EditorQueryDesc),
        EDITOR_TEXT("appearance.values"),
        EDITOR_TEXT("builtin.application"),
        &themeArgument,
        1,
        [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
            const auto &state = *static_cast<State *>(data);
            std::string id = "editor.theme";
            for (uint32_t i = 0; i < request->count; ++i)
                if (String(request->values[i].key) == "theme" && request->values[i].text.size)
                    id = String(request->values[i].text);
            const auto found = state.configuration.Get().themeValues.find(id);
            if (found == state.configuration.Get().themeValues.end()) return EDITOR_INVALID;
            EditorField record{"values", EDITOR_RECORD};
            for (const auto &entry : found->second) record.fields.push_back(entry.second);
            const std::vector<EditorField> owned{std::move(record)};
            const auto values = BorrowEditorFields(owned);
            return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
        },
        m_State.get(),
        EDITOR_QUERY_VERSIONED,
        EDITOR_APPLICATION,
        &themeField,
        1,
        [](void *data, const EditorHost *) { return static_cast<State *>(data)->configuration.Get().appearanceRevision; }};
    const EditorQueryDesc queries[] = {query, output, appearance};
    EditorModuleDesc module{};
    module.size = sizeof(module); module.version = EDITOR_VERSION;
    module.id = EditorLiteral("application.operations");
    module.queries = queries;
    module.queryCount = 3;
    std::string error;
    if (!Register(module, error)) throw std::logic_error(error);
}
EditorApplication::~EditorApplication() { Stop(); }
const EditorHost *EditorApplication::Host() const { return &m_State->host; }
void EditorApplication::ViewNavigation(EditorViewNavigation *navigation) {
    if (m_State->Thread()) m_State->navigation = navigation;
}
void *EditorApplication::CreateView(const EditorViewDesc &desc, EditorView id) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active || !s.scopes.count(id)) return nullptr;
    if (desc.area == EDITOR_DOCUMENT && !s.NativeReady()) return nullptr;
    ++s.readOnlyCallbacks;
    ++s.drawing;
    void *data = nullptr;
    try { data = desc.create(desc.data, Host(), id); } catch (...) {}
    --s.drawing;
    --s.readOnlyCallbacks;
    if (data) {
        s.toolStates[id].type = String(desc.id);
        s.toolStates[id].fallback = String(desc.defaultTool);
        if (desc.defaultTool.size) RestoreTool(id, String(desc.defaultTool));
    }
    return data;
}
EditorStatus EditorApplication::CanCloseView(const EditorViewDesc &desc, void *data, EditorView id) {
    auto &s = *m_State;
    if (!s.Thread() || s.readOnlyCallbacks || s.querying || s.drawing || !s.scopes.count(id) ||
        (desc.area == EDITOR_DOCUMENT && !s.NativeReady()))
        return EDITOR_REFUSED;
    if (!desc.canClose)
        return EDITOR_OK;
    ++s.readOnlyCallbacks;
    ++s.querying;
    EditorStatus status = EDITOR_ERROR;
    try { status = desc.canClose(data, Host(), id); } catch (...) {}
    --s.querying;
    --s.readOnlyCallbacks;
    return status;
}
bool EditorApplication::ClosingView(const EditorViewDesc &desc, void *data, EditorView id) {
    auto &s = *m_State;
    if (!s.Thread() || s.readOnlyCallbacks || s.querying || s.drawing || !s.scopes.count(id))
        return false;
    if (!desc.closing)
        return true;
    ++s.executing;
    try {
        desc.closing(data, Host(), id);
        --s.executing;
        return true;
    } catch (...) {
        --s.executing;
        return false;
    }
}
bool EditorApplication::DestroyView(const EditorViewDesc &desc, void *data) {
    auto &s = *m_State;
    if (!s.Thread()) return false;
    ++s.readOnlyCallbacks;
    ++s.drawing;
    bool destroyed = true;
    try { desc.destroy(data); } catch (...) { destroyed = false; }
    --s.drawing;
    --s.readOnlyCallbacks;
    return destroyed;
}
EditorResponse EditorApplication::SaveViewState(const EditorViewDesc &desc, void *data, EditorScope scope) {
    EditorResponse result;
    auto &s = *m_State;
    if (!desc.saveState) { result.status = EDITOR_NO_CHANGE; return result; }
    if (!s.Thread() || !s.Valid(scope, desc.area) || (desc.area == EDITOR_DOCUMENT && !s.NativeReady())) {
        result.status = EDITOR_STALE;
        return result;
    }
    const EditorRequest request{scope, nullptr, 0};
    Output output{result.fields};
    ++s.querying;
    ++s.readOnlyCallbacks;
    try { result.status = desc.saveState(data, Host(), &request, output.Sink()); }
    catch (...) { result.status = EDITOR_ERROR; }
    --s.readOnlyCallbacks;
    --s.querying;
    if (output.status != EDITOR_OK) result.status = output.status;
    if (desc.documentBound ? !s.CurrentValues(result.fields) : !EveryField(result.fields, [](const auto &field) {
            return field.type != EDITOR_OBJECT && field.type != EDITOR_OBJECT_LIST;
        }))
        result.status = EDITOR_INVALID;
    if (result.status != EDITOR_OK) result.fields.clear();
    return result;
}
EditorStatus EditorApplication::RestoreViewState(const EditorViewDesc &desc, void *data, EditorScope scope,
                                                 const std::vector<EditorField> &state) {
    auto &s = *m_State;
    if (!s.Thread() || !s.Valid(scope, desc.area) || (desc.area == EDITOR_DOCUMENT && !s.NativeReady())) return EDITOR_STALE;
    if (!desc.restoreState) return EDITOR_INVALID;
    if (desc.documentBound ? !s.CurrentValues(state) : !EveryField(state, [](const auto &field) {
            return field.type != EDITOR_OBJECT && field.type != EDITOR_OBJECT_LIST;
        }))
        return EDITOR_STALE;
    const auto values = BorrowEditorFields(state);
    const EditorRequest request{scope, values.data(), uint32_t(values.size())};
    ++s.drawing;
    ++s.readOnlyCallbacks;
    const auto previousView = s.restoringView;
    s.restoringView = scope.view;
    EditorStatus status;
    try { status = desc.restoreState(data, Host(), &request); }
    catch (...) { status = EDITOR_ERROR; }
    s.restoringView = previousView;
    --s.readOnlyCallbacks;
    --s.drawing;
    return status;
}
EditorResponse EditorApplication::SaveViewSettings(const EditorViewDesc &desc, void *data, EditorScope scope) {
    if (!m_State->Thread() || !m_State->Valid(scope, desc.area))
        return {EDITOR_STALE};
    if (!desc.settingCount)
        return {EDITOR_OK};
    auto result = SaveViewState(desc, data, scope);
    if (result.status != EDITOR_OK)
        return result;
    std::vector<EditorField> fields;
    for (uint32_t i = 0; i < desc.settingCount; ++i) {
        const auto &schema = desc.settings[i];
        const auto field = std::find_if(result.fields.begin(), result.fields.end(),
                                        [&](const auto &value) { return value.key == String(schema.key); });
        if (field == result.fields.end()) {
            if (schema.optional)
                continue;
            return {EDITOR_INVALID};
        }
        if (field->type != schema.type || !EveryField(std::vector<EditorField>{*field}, [](const auto &value) {
                return value.type != EDITOR_OBJECT && value.type != EDITOR_OBJECT_LIST;
            }))
            return {EDITOR_INVALID};
        fields.push_back(*field);
    }
    result.fields = std::move(fields);
    return result;
}
EditorStatus EditorApplication::RestoreViewSettings(const EditorViewDesc &desc, void *data, EditorScope scope,
                                                    const std::vector<EditorField> &settings) {
    if (!m_State->Thread() || !m_State->Valid(scope, desc.area))
        return EDITOR_STALE;
    if (settings.empty())
        return EDITOR_OK;
    if (!desc.settingCount || !EveryField(settings, [](const auto &value) {
            return value.type != EDITOR_OBJECT && value.type != EDITOR_OBJECT_LIST;
        }))
        return EDITOR_INVALID;
    auto state = SaveViewState(desc, data, scope);
    if (state.status != EDITOR_OK)
        return state.status;
    for (const auto &field : settings) {
        const auto schema = std::find_if(desc.settings, desc.settings + desc.settingCount, [&](const auto &value) {
            return String(value.key) == field.key && value.type == field.type;
        });
        if (schema == desc.settings + desc.settingCount)
            return EDITOR_INVALID;
        auto target = std::find_if(state.fields.begin(), state.fields.end(),
                                   [&](const auto &value) { return value.key == field.key; });
        if (target == state.fields.end())
            state.fields.push_back(field);
        else
            *target = field;
    }
    return RestoreViewState(desc, data, scope, state.fields);
}
uint64_t EditorApplication::InspectRevision(const EditorViewDesc &desc, void *data) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active) return 0;
    ++s.readOnlyCallbacks;
    ++s.querying;
    uint64_t version = 0;
    try { if (desc.inspectRevision) version = desc.inspectRevision(data, Host()); }
    catch (...) { --s.querying; --s.readOnlyCallbacks; throw; }
    --s.querying;
    --s.readOnlyCallbacks;
    return version;
}
EditorResponse EditorApplication::InspectView(const EditorViewDesc &desc, void *data, EditorScope scope) {
    auto &s = *m_State;
    EditorResponse result;
    if (!s.Thread() || !s.Valid(scope, EDITOR_WORKSPACE)) { result.status = EDITOR_STALE; return result; }
    if (!desc.inspect) { result.status = EDITOR_NO_CHANGE; return result; }
    Output output{result.fields};
    const EditorRequest request{scope, nullptr, 0};
    ++s.querying;
    ++s.readOnlyCallbacks;
    try { result.status = desc.inspect(data, Host(), &request, output.Sink()); }
    catch (...) { result.status = EDITOR_ERROR; }
    --s.readOnlyCallbacks;
    --s.querying;
    if (output.status != EDITOR_OK) result.status = output.status;
    if (result.status == EDITOR_OK && result.fields.size() != desc.fieldCount) result.status = EDITOR_INVALID;
    for (uint32_t i = 0; i < desc.fieldCount && result.status == EDITOR_OK; ++i) {
        const auto field = std::find_if(result.fields.begin(), result.fields.end(), [&](const EditorField &f) {
            return f.key == String(desc.fields[i].key) && f.type == desc.fields[i].type;
        });
        if (field == result.fields.end()) result.status = EDITOR_INVALID;
    }
    if (!EveryField(result.fields, [&](const auto &field) {
        if (field.type == EDITOR_OBJECT && field.object && field.object.document != scope.document) return false;
        return field.type != EDITOR_OBJECT_LIST || std::all_of(field.objects.begin(), field.objects.end(), [&](auto object) { return !object || object.document == scope.document; });
    })) result.status = EDITOR_STALE;
    if (result.status != EDITOR_OK) result.fields.clear();
    return result;
}
EditorResponse EditorApplication::PickView(const EditorViewDesc &desc, void *data, EditorScope scope, float x, float y,
                                           uint32_t objectClass) {
    auto &s = *m_State;
    EditorResponse result;
    if (!s.Thread() || !s.NativeReady() || s.querying >= 16) {
        result.status = EDITOR_REFUSED;
        return result;
    }
    if (!s.Valid(scope, EDITOR_DOCUMENT)) {
        result.status = EDITOR_STALE;
        return result;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        result.status = EDITOR_INVALID;
        return result;
    }
    if (!desc.pick) {
        result.status = EDITOR_NO_CHANGE;
        return result;
    }
    EditorValue coordinates[3] = {{EDITOR_TEXT("x"), EDITOR_REAL_VALUE},
                                  {EDITOR_TEXT("y"), EDITOR_REAL_VALUE},
                                  {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE, {}, {}, objectClass}};
    coordinates[0].real = x;
    coordinates[1].real = y;
    const EditorRequest request{scope, coordinates, objectClass ? 3u : 2u};
    Output output{result.fields};
    ++s.querying;
    ++s.readOnlyCallbacks;
    try {
        result.status = desc.pick(data, Host(), &request, output.Sink());
    } catch (...) {
        result.status = EDITOR_ERROR;
    }
    --s.readOnlyCallbacks;
    --s.querying;
    if (output.status != EDITOR_OK)
        result.status = output.status;
    if (result.status == EDITOR_OK) {
        if (result.fields.size() != 1 || result.fields.front().key != "objects" ||
            result.fields.front().type != EDITOR_OBJECT_LIST)
            result.status = EDITOR_INVALID;
        else if (!s.Valid(scope, EDITOR_DOCUMENT) ||
                 !std::all_of(result.fields.front().objects.begin(), result.fields.front().objects.end(),
                              [&](ObjectRef object) { return object && s.Current(object); }))
            result.status = EDITOR_STALE;
    }
    if (result.status != EDITOR_OK)
        result.fields.clear();
    return result;
}
EditorStatus EditorApplication::QueryRevision(const std::string &id, uint64_t &version) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active || s.readOnlyCallbacks || s.querying>=16) return EDITOR_REFUSED;
    s.RefreshDocument();
    const auto found = s.configuration.Get().queries.find(id);
    if (found == s.configuration.Get().queries.end()) return EDITOR_INVALID;
    const auto &desc = *found->second;
    if (desc.refresh == EDITOR_QUERY_EACH_READ || s.querying >= 16) return EDITOR_REFUSED;
    uint64_t value = s.published;
    ++s.querying;
    ++s.readOnlyCallbacks;
    try { if (desc.revision) value = desc.revision(desc.data, Host()); }
    catch (...) { --s.querying; --s.readOnlyCallbacks; return EDITOR_ERROR; }
    --s.querying;
    --s.readOnlyCallbacks;
    auto &revision = s.queryRevisions[id];
    if (revision.source != &desc || revision.value != value) {
        revision.source = &desc; revision.value = value; ++revision.published;
    }
    version = revision.published;
    return EDITOR_OK;
}

bool EditorApplication::Register(const EditorModuleDesc &module, std::string &error) {
    auto &s = *m_State;
    if (!s.Thread() || s.active || s.stopped) { error = "Registration is closed"; return false; }
    return s.modules.Register(module, error);
}
bool EditorApplication::Load(const std::string &path, std::string &error) {
    auto &s = *m_State;
    if (!s.Thread() || s.active || s.stopped) { error = "Registration is closed"; return false; }
    return s.modules.Load(Host(), path, error);
}
bool EditorApplication::Assemble(const std::vector<EditorModuleSource> &sources, EditorDrawingVersion drawing,
                                 std::string &error, const EditorConfiguration &configuration,
                                 const std::vector<EditorModuleSetting> &settings, const EditorConfiguration &overrides) {
    auto &s = *m_State;
    if (!s.Thread() || s.active || s.stopped) { error = "Registration is closed"; return false; }
    return s.modules.Assemble(*this, sources, drawing, error, configuration, settings, overrides);
}
std::vector<EditorModuleInfo> EditorApplication::Modules() const {
    return m_State->Thread() ? m_State->modules.Information() : std::vector<EditorModuleInfo>{};
}
const EditorConfiguration &EditorApplication::DefaultConfiguration() const { return m_State->modules.Defaults(); }
std::string EditorApplication::ProviderModule(const ProviderChoice &choice) const {
    if (!m_State->Thread()) return {};
    if (choice.kind == "toolbar-item") {
        for (const auto &module : m_State->modules.All())
            for (const auto &item : module->toolbarItems)
                if (String(item.id) == choice.id) return String(module->id);
        for (const auto &item : m_State->configuration.Get().toolbarItems)
            if (item.id == choice.id && !item.action.empty()) return ProviderModule({"action", item.action, {}});
        return {};
    }
    if (choice.kind == "menu") {
        for (const auto &module : m_State->modules.All()) for (const auto &menu : module->menus)
            if (String(menu.id) == choice.id) return String(module->id);
        return {};
    }
    if (choice.kind == "action") {
        auto id = choice.id;
        if (auto property = m_State->configuration.Get().properties.find(id); property != m_State->configuration.Get().properties.end()) id = property->second.command;
        if (auto command = m_State->configuration.Get().commands.find(id); command != m_State->configuration.Get().commands.end())
            return ProviderModule({"command", id, String(command->second->provider)});
        return {};
    }
    const auto find = [&](auto member) -> std::string {
        for (const auto &module : m_State->modules.All()) for (const auto &desc : module.get()->*member)
            if (String(desc.id) == choice.id && String(desc.provider) == choice.provider) return String(module->id);
        return {};
    };
    return choice.kind == "command"   ? find(&Module::commands)
           : choice.kind == "query"   ? find(&Module::queries)
           : choice.kind == "editor"  ? find(&Module::editors)
           : choice.kind == "view"    ? find(&Module::views)
           : choice.kind == "theme"   ? find(&Module::themes)
           : choice.kind == "font"    ? find(&Module::fonts)
           : choice.kind == "document-data" ? find(&Module::documentData)
           : choice.kind == "image"   ? find(&Module::images)
           : choice.kind == "toolbar" ? find(&Module::toolbarItems)
                                      : std::string{};
}
std::shared_ptr<const DocumentDataRegistry> EditorApplication::DataTypes() const {
    return m_State->Thread() ? m_State->configuration.Get().dataTypes : DefaultDocumentDataRegistry();
}
bool EditorApplication::Activate(EditorDrawingVersion imguiVersion, std::string &error,
                                 const EditorConfiguration &configuration) {
    auto &s = *m_State;
    if (!s.Thread() || s.active || s.stopped || s.modules.Busy()) {
        error = "Registration is closed";
        return false;
    }
    try {
        s.modules.PrepareActivation(imguiVersion);
        s.active = true;
        if (!Configure(configuration, error)) {
            s.active = false;
            s.configuration.Clear();
            s.modules.ClearActivation();
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        s.active = false;
        s.configuration.Clear();
        s.modules.ClearActivation();
        error = e.what();
        return false;
    }
}
bool EditorApplication::Configure(const EditorConfiguration &configuration, std::string &error) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active || s.modules.Stopped() || s.drawing || s.querying || s.executing || s.notifying) {
        error = "Finish active callbacks before changing configuration";
        return false;
    }
    try {
        auto selected = s.configuration.Prepare(s.modules.Active(), configuration);
        const bool editing = !s.drafts.empty() ||
            std::any_of(s.pending.begin(), s.pending.end(), [](const auto &p) { return bool(p.preview); });
        const bool toolActive = std::any_of(s.toolStates.begin(), s.toolStates.end(),
            [](const auto &entry) { return entry.second.captured || entry.second.dispatching; });
        if ((editing || toolActive) && !s.configuration.EditingProvidersUnchanged(selected))
            throw std::invalid_argument("Finish active edits and tools before replacing their providers");
        if (s.configuration.Get().documentData != selected.documentData) {
            if (s.document.value)
                throw std::invalid_argument("Close the composition before replacing document data providers");
            auto registry = std::make_shared<DocumentDataRegistry>(*DefaultDocumentDataRegistry());
            for (const auto *module : s.modules.Active())
                for (const auto &desc : module->documentData)
                    if (selected.documentData.count(String(desc.id)) && selected.documentData.at(String(desc.id)) == &desc)
                        if (!registry->Register(MakeDocumentDataType(desc, module->dataCallbacks), error))
                            throw std::invalid_argument(error);
            selected.dataTypes = std::move(registry);
        }
        if (s.configuration.Get().dataTypes != selected.dataTypes)
            if (auto *doc = s.CurrentDocument())
                if (!s.NativeReady() || !doc->SetDataTypes(selected.dataTypes))
                    throw std::invalid_argument("Finish file work and close the composition before replacing document data providers");
        s.configuration.Install(std::move(selected));
        s.cache.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
const EditorConfiguration &EditorApplication::Configuration() const { return m_State->configuration.Get().configuration; }
void EditorApplication::Attach(DocumentToken document, uint64_t revision, bool available, bool writable) {
    auto &s = *m_State;
    if (!s.Thread())
        return;
    const bool documentChanged = s.document != document;
    if (documentChanged) {
        for (const auto &entry : s.toolStates) CancelTool(entry.first);
        s.ClearDrafts();
        for (const auto &pending : s.pending) s.Complete(pending.operation, {EDITOR_STALE});
        s.pending.clear();
        s.selection = {};
        s.cache.clear();
    }
    s.available = available;
    auto *current = s.NativeReady() ? s.CurrentDocument() : nullptr;
    const bool contentChanged = current && current->Token() == document && !current->EditOpen() &&
                                current->ChangeRevision() != s.contentRevision;
    if (s.revision != revision || s.document != document || contentChanged) {
        s.cache.clear();
        ++s.published;
    }
    s.document = document;
    s.revision = revision;
    if (current && current->Token() == document && !current->EditOpen()) s.contentRevision = current->ChangeRevision();
    s.writable = writable;
    if (available) {
        for (auto it = s.drafts.begin(); it != s.drafts.end();)
            if (!s.Current(it->second.object)) {
                s.RestorePreview(it->second);
                it = s.drafts.erase(it);
            } else
                ++it;
    }
    if (documentChanged)
        for (auto &watch : s.watches)
            watch.second.scope.document = document;
}
void EditorApplication::NativeAvailable(bool available) { m_State->available = available; }
bool EditorApplication::NativeAvailable() const { return m_State->Thread() && m_State->NativeReady(); }
uint64_t EditorApplication::Revision() const {
    if (!m_State->Thread()) return 0;
    m_State->RefreshDocument();
    return m_State->published;
}
bool EditorApplication::HasEdits() const {
    return !m_State->drafts.empty() || !m_State->pending.empty() ||
           std::any_of(m_State->toolStates.begin(), m_State->toolStates.end(),
                       [](const auto &entry) { return entry.second.captured; });
}
ObjectRef EditorApplication::EditTarget(EditorEdit edit) const {
    auto f = m_State->drafts.find(edit);
    return f == m_State->drafts.end() ? ObjectRef{} : f->second.object;
}
void EditorApplication::SetSelection(ObjectRef object) { m_State->selection = object; }
ObjectRef EditorApplication::Selection() const { return m_State->Thread() ? m_State->selection : ObjectRef{}; }
EditorView EditorApplication::OpenScope() {
    auto &s = *m_State;
    if (!s.Thread() || !s.active)
        return 0;
    const auto id = s.next++;
    s.scopes.insert(id);
    return id;
}
void EditorApplication::CloseScope(EditorView view) {
    auto &s = *m_State;
    if (!s.Thread())
        return;
    CancelTool(view);
    if (auto tool = s.toolStates.find(view);
        tool != s.toolStates.end() && tool->second.handler && tool->second.entered) {
        EditorToolInput input{};
        input.size = sizeof(input);
        input.phase = EDITOR_TOOL_LEAVE;
        ToolInput(Scope(view), input);
    }
    s.toolStates.erase(view);
    s.scopes.erase(view);
    s.queryResult = {};
    for (auto &query : s.cache)
        query.second.erase(std::remove_if(query.second.begin(), query.second.end(),
            [&](const State::Cached &entry) { return entry.scope.view == view; }), query.second.end());
    for (auto it = s.watches.begin(); it != s.watches.end();)
        if (it->second.scope.view == view)
            it = s.watches.erase(it);
        else
            ++it;
    for (auto it = s.drafts.begin(); it != s.drafts.end();)
        if (it->second.scope.view == view) {
            s.RestorePreview(it->second);
            it = s.drafts.erase(it);
        } else
            ++it;
    s.pending.erase(std::remove_if(s.pending.begin(), s.pending.end(),
                                   [&](const State::Pending &command) {
                                       if (command.scope.view != view) return false;
                                       s.Complete(command.operation, {EDITOR_STALE});
                                       return true;
                                   }),
                    s.pending.end());
}
EditorScope EditorApplication::Scope(EditorView view) const {
    auto &s = *m_State;
    return s.Thread() && s.active && (!view || s.scopes.count(view)) ? EditorScope{s.document, view} : EditorScope{};
}
EditorResponse EditorApplication::Query(const std::string &id, const EditorRequest &request) {
    const auto &result = ReadQuery(id, request);
    if (m_State->Thread() && &result == &m_State->queryResult) return std::move(m_State->queryResult);
    return result;
}
const EditorResponse &EditorApplication::ReadQuery(const std::string &id, const EditorRequest &request) {
    return ReadQuery(id, request, nullptr);
}
EditorStatus EditorApplication::DeliverQuery(std::string_view id, const EditorRequest &request, EditorSink sink) {
    // A sink may execute commands and clear the query cache while reading the
    // result. Retain the immutable cached value through that callback, without
    // copying its fields on each delivery.
    std::shared_ptr<const CachedQuery> retained;
    const auto &result = ReadQuery(id, request, &retained);
    if (retained)
        return Deliver(retained->response, retained->values, sink, true);
    if (m_State->Thread() && &result == &m_State->queryResult) {
        auto owned = std::move(m_State->queryResult);
        return Send(owned, sink);
    }
    return Send(result, sink);
}
const EditorResponse &EditorApplication::ReadQuery(std::string_view id, const EditorRequest &request,
                                                  std::shared_ptr<const CachedQuery> *retained) {
    auto &s = *m_State;
    EditorResponse result;
    if (!s.Thread()) {
        result.status = EDITOR_REFUSED;
        static const EditorResponse refused{EDITOR_REFUSED};
        return refused;
    }
    s.RefreshDocument();
    const auto transient = [&]() -> const EditorResponse & { s.queryResult = std::move(result); return s.queryResult; };
    auto found = s.configuration.Get().queries.find(id);
    if (found == s.configuration.Get().queries.end()) {
        result.status = EDITOR_INVALID;
        return transient();
    }
    std::vector<EditorField> fields;
    const EditorQueryDesc *query = found->second;
    if (request.handler.size) {
        const auto handler = String(request.handler);
        query = nullptr;
        for (auto &module : s.modules.All())
            for (auto &candidate : module->queries)
                if (String(candidate.id) == id && String(candidate.provider) == handler)
                    query = &candidate;
        if (!query) {
            result.status = EDITOR_INVALID;
            return transient();
        }
    }
    result.status = s.Arguments(query->parameters, query->count, request, fields, query->area,
                                !(query->flags & EDITOR_QUERY_RETAINED_REFERENCES));
    if (result.status != EDITOR_OK)
        return transient();
    uint64_t version = 0;
    if (query->refresh == EDITOR_QUERY_VERSIONED) {
        if (s.querying >= 16) { result.status = EDITOR_REFUSED; return transient(); }
        ++s.querying;
        ++s.readOnlyCallbacks;
        try { version = query->revision(query->data, Host()); }
        catch (...) { result.status = EDITOR_ERROR; }
        --s.readOnlyCallbacks;
        --s.querying;
        if (result.status != EDITOR_OK) return transient();
    }
    auto &cache = s.cache[query];
    const auto same = [&](const State::Cached &c) {
        if (c.scope.document != request.scope.document || c.scope.view != request.scope.view ||
            c.arguments.size() != fields.size())
            return false;
        for (size_t i = 0; i < fields.size(); ++i)
            if (c.arguments[i] != fields[i])
                return false;
        return true;
    };
    if (query->refresh != EDITOR_QUERY_EACH_READ)
        if (auto previous = std::find_if(cache.begin(), cache.end(), same); previous != cache.end() && previous->version == version) {
            if (retained) *retained = previous->result;
            return previous->result->response;
        }
    if ((!s.NativeReady() && query->area == EDITOR_DOCUMENT) || s.querying >= 16) {
        result.status = EDITOR_REFUSED;
        return transient();
    }
    ++s.querying;
    Output output{result.fields};
    try {
        result.status = query->read(query->data, Host(), &request, output.Sink());
    } catch (...) {
        result.status = EDITOR_ERROR;
        result.message = "Query callback raised an exception";
    }
    --s.querying;
    if (output.status != EDITOR_OK)
        result.status = output.status;
    if (result.status == EDITOR_OK) {
        if (result.fields.size() != query->fieldCount)
            result.status = EDITOR_INVALID;
        for (uint32_t i = 0; i < query->fieldCount && result.status == EDITOR_OK; ++i) {
            auto field = std::find_if(result.fields.begin(), result.fields.end(), [&](const EditorField &field) {
                return SameText(field.key, query->fields[i].key);
            });
            if (field == result.fields.end() || field->type != query->fields[i].type)
                result.status = EDITOR_INVALID;
        }
    }
    if (result.status != EDITOR_OK)
        result.fields.clear();
    if (result.status == EDITOR_OK && query->refresh != EDITOR_QUERY_EACH_READ) {
        if (auto previous = std::find_if(cache.begin(), cache.end(), same); previous != cache.end()) {
            previous->result = std::make_shared<const CachedQuery>(std::move(result));
            previous->version = version;
            if (retained) *retained = previous->result;
            return previous->result->response;
        }
        if (cache.size() >= 256) cache.erase(cache.begin());
        cache.push_back({request.scope, std::move(fields), std::make_shared<const CachedQuery>(std::move(result)), version});
        if (retained) *retained = cache.back().result;
        return cache.back().result->response;
    }
    return transient();
}
EditorStatus EditorApplication::CommandState(const std::string &id, const EditorRequest &input) {
    auto &s = *m_State;
    if (!s.Thread())
        return EDITOR_REFUSED;
    auto request = input;
    const auto routed = s.ResolveCommandView(id, request.scope);
    if (routed != EDITOR_OK) return routed;
    auto found = s.configuration.Get().commands.find(id);
    if (found == s.configuration.Get().commands.end())
        return EDITOR_INVALID;
    const EditorCommandDesc *command = found->second;
    if (request.handler.size) {
        command = nullptr;
        const auto handler = String(request.handler);
        for (auto &module : s.modules.All())
            for (auto &candidate : module->commands)
                if (String(candidate.id) == id && String(candidate.provider) == handler)
                    command = &candidate;
        if (!command)
            return EDITOR_INVALID;
    }
    std::vector<EditorField> fields;
    const auto status = s.Arguments(command->parameters, command->count, request, fields, command->area);
    return status == EDITOR_OK ? s.Check(*command, request.scope, fields) : status;
}
EditorStatus EditorApplication::CanEdit(const std::string &property, EditorScope scope, ObjectRef target) const {
    auto &s = *m_State;
    if (!s.Thread())
        return EDITOR_REFUSED;
    if (s.readOnlyCallbacks) return EDITOR_REFUSED;
    if (!s.Valid(scope) || !target || target.document != scope.document)
        return EDITOR_STALE;
    if (!s.configuration.Get().properties.count(property))
        return EDITOR_INVALID;
    if (!s.NativeReady() || !s.writable || !s.drafts.empty() || (!s.pending.empty() && !s.drawing))
        return EDITOR_REFUSED;
    const auto *doc = s.CurrentDocument();
    if (!doc || doc->EditOpen())
        return EDITOR_REFUSED;
    return s.Current(target) ? EDITOR_OK : EDITOR_STALE;
}
EditorResponse EditorApplication::Execute(const std::string &id, const EditorRequest &input) {
    auto &s = *m_State;
    EditorResponse result;
    if (!s.Thread() || s.readOnlyCallbacks || s.checking) {
        result.status = EDITOR_REFUSED;
        return result;
    }
    auto request = input;
    const auto routed = s.ResolveCommandView(id, request.scope);
    if (routed != EDITOR_OK) return {routed};
    auto found = s.configuration.Get().commands.find(id);
    if (found == s.configuration.Get().commands.end()) {
        result.status = EDITOR_INVALID;
        return result;
    }
    const EditorCommandDesc *command = found->second;
    if (request.handler.size) {
        const auto handler = String(request.handler);
        command = nullptr;
        for (auto &module : s.modules.All())
            for (auto &candidate : module->commands)
                if (String(candidate.id) == id && String(candidate.provider) == handler)
                    command = &candidate;
        if (!command) {
            result.status = EDITOR_INVALID;
            return result;
        }
    }
    std::vector<EditorField> fields;
    result.status = s.Arguments(command->parameters, command->count, request, fields, command->area);
    if (result.status != EDITOR_OK) return result;
    auto admission = *command;
    if (request.afterEdit)
        admission.state = nullptr;
    result.status = s.Check(admission, request.scope, fields);
    if (result.status != EDITOR_OK) return result;
    const bool deferred = s.drawing || s.querying || s.notifying || (!s.executing && !s.pending.empty());
    const auto revisionArgument = String(request.revisionArgument);
    if (request.afterEdit) {
        const auto parent = std::find_if(s.pending.begin(), s.pending.end(), [&](const auto &pending) {
            return pending.operation == request.afterEdit && pending.scope.document == request.scope.document &&
                   pending.command->effect == EDITOR_DOCUMENT_EDIT;
        });
        const auto recorded = s.operations.find(request.afterEdit);
        if (recorded == s.operations.end() || !recorded->second.documentEdit ||
            recorded->second.scope.document != request.scope.document)
            return {EDITOR_STALE};
        if (parent == s.pending.end()) {
            if (recorded->second.result.status != EDITOR_OK && recorded->second.result.status != EDITOR_NO_CHANGE)
                return {recorded->second.result.status == EDITOR_QUEUED ? EDITOR_STALE
                                                                        : recorded->second.result.status};
        } else if (!deferred)
            return {EDITOR_STALE};
        if (!revisionArgument.empty() && std::none_of(fields.begin(), fields.end(), [&](const auto &field) {
                return field.key == revisionArgument && field.type == EDITOR_INTEGER_VALUE;
            }))
            return {EDITOR_INVALID};
    } else if (!revisionArgument.empty())
        return {EDITOR_INVALID};
    if (s.operations.size() >= s.completedOperations.size() + 256 || s.pending.size() >= 256) {
        result.status = EDITOR_REFUSED;
        result.message = "Too many pending commands";
        return result;
    }
    // A view/session action may start an independent document command. Only
    // nested document edits share their outer edit and completion number.
    const bool sharesOperation = s.activeOperation && (s.editing || command->effect != EDITOR_DOCUMENT_EDIT);
    const EditorOperation operation = !deferred && sharesOperation ? s.activeOperation : s.next++;
    if (!s.operations.count(operation)) {
        s.operations.emplace(operation, State::Operation{EditorResponse{EDITOR_QUEUED}, command->effect == EDITOR_DOCUMENT_EDIT});
        s.operations.at(operation).scope = request.scope;
    }
    if (deferred) {
        s.pending.push_back(
            {command, request.scope, std::move(fields), operation, {}, request.afterEdit, revisionArgument});
        result.status = EDITOR_QUEUED;
        result.operation = operation;
        return result;
    }
    if (request.afterEdit)
        for (auto &field : fields)
            if (field.key == revisionArgument)
                field.integer = s.operations.at(request.afterEdit).result.documentRevision;
    return s.Run(*command, request.scope, fields, operation);
}
bool EditorApplication::CompleteOperation(EditorOperation operation, EditorResponse result) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active || s.readOnlyCallbacks || s.checking) return false;
    const auto found = s.operations.find(operation);
    return found != s.operations.end() && !found->second.documentEdit && s.Complete(operation, std::move(result));
}
EditorStatus EditorApplication::BeginEdit(const std::string &property, EditorScope scope, ObjectRef target,
                                          EditorEdit &out) {
    const EditorValue argument{EditorLiteral("target"), EDITOR_OBJECT, target};
    return BeginEdit(property, {scope, &argument, 1}, out);
}
EditorStatus EditorApplication::BeginEdit(const std::string &property, const EditorRequest &request, EditorEdit &out) {
    auto &s = *m_State;
    out = 0;
    if (!s.Thread() || (s.inputView && request.scope.view != s.inputView)) return EDITOR_REFUSED;
    if (!s.Thread() || s.readOnlyCallbacks || s.querying || s.notifying) return EDITOR_REFUSED;
    std::vector<EditorField> arguments;
    if (CopyEditorFields(arguments, request.values, request.count) != EDITOR_OK) return EDITOR_INVALID;
    const auto targetField = std::find_if(arguments.begin(), arguments.end(), [](const auto &f) { return f.key == "target" && f.type == EDITOR_OBJECT; });
    if (targetField == arguments.end()) return EDITOR_INVALID;
    const auto target = targetField->object;
    const auto status = CanEdit(property, request.scope, target);
    if (status != EDITOR_OK) return status;
    const auto &p = s.configuration.Get().properties.at(property);
    auto query = request; query.handler = EditorText(p.queryProvider);
    auto result = Query(p.query, query);
    if (result.status != EDITOR_OK) return result.status;
    const auto value = std::find_if(result.fields.begin(), result.fields.end(), [&](const EditorField &f) {
        return f.key == p.field && f.type == p.type;
    });
    if (value == result.fields.end()) return EDITOR_INVALID;
    State::Draft draft{request.scope, target, p, s.configuration.Get().commands.at(p.command), s.configuration.Get().editors.at(p.editor), *value, std::move(result.fields), s.configuration.Get().editors, std::move(arguments)};
    out = s.next++;
    draft.deferred = !s.pending.empty();
    draft.toolOwned = s.inputView == request.scope.view && s.inputView != 0;
    s.drafts.emplace(out, std::move(draft));
    return EDITOR_OK;
}
EditorStatus EditorApplication::BeginCommand(const std::string &id, const EditorRequest &input, EditorEdit &out) {
    auto &s = *m_State;
    out = 0;
    auto request = input;
    if (!s.Thread() || (s.inputView && request.scope.view != s.inputView)) return EDITOR_REFUSED;
    if (!s.Thread() || s.readOnlyCallbacks || s.querying || s.notifying || !s.NativeReady() || !s.writable ||
        !s.drafts.empty() || !s.pending.empty())
        return EDITOR_REFUSED;
    const auto routed = s.ResolveCommandView(id, request.scope);
    if (routed != EDITOR_OK) return routed;
    const auto selected = s.configuration.Get().commands.find(id);
    const auto *command = selected == s.configuration.Get().commands.end() ? nullptr : selected->second;
    if (request.handler.size) {
        command = nullptr;
        for (const auto &module : s.modules.All())
            for (const auto &candidate : module->commands)
                if (String(candidate.id) == id && String(candidate.provider) == String(request.handler))
                    command = &candidate;
    }
    if (!command || !command->preview || !command->commitPreview || command->effect != EDITOR_DOCUMENT_EDIT)
        return EDITOR_INVALID;
    std::vector<EditorField> fields;
    auto status = s.Arguments(command->parameters, command->count, request, fields, command->area);
    if (status != EDITOR_OK)
        return status;
    const auto target = std::find_if(fields.begin(), fields.end(),
                                     [](const auto &f) { return f.key == "target" && f.type == EDITOR_OBJECT; });
    const auto value = std::find_if(fields.begin(), fields.end(), [](const auto &f) { return f.key == "value"; });
    if (target == fields.end() || !target->object || value == fields.end())
        return EDITOR_INVALID;
    status = s.Check(*command, request.scope, fields);
    if (status != EDITOR_OK)
        return status;
    State::Draft draft{};
    draft.scope = request.scope;
    draft.object = target->object;
    draft.command = command;
    draft.value = *value;
    draft.property.type = value->type;
    draft.arguments = std::move(fields);
    draft.commandOnly = true;
    out = s.next++;
    draft.toolOwned = s.inputView == request.scope.view && s.inputView != 0;
    s.drafts.emplace(out, std::move(draft));
    return EDITOR_OK;
}
EditorStatus EditorApplication::UpdateEdit(EditorEdit edit, const std::string &text) {
    const EditorValue value{EditorLiteral("value"), EDITOR_TEXT_VALUE, {}, EditorText(text)};
    return UpdateEdit(edit, value);
}
EditorStatus EditorApplication::UpdateEdit(EditorEdit edit, const EditorValue &value) {
    auto &s = *m_State;
    if (!s.Thread() || s.readOnlyCallbacks || s.querying || s.notifying) return EDITOR_REFUSED;
    const auto found = s.drafts.find(edit);
    if (found == s.drafts.end())
        return EDITOR_STALE;
    if (s.inputView && found->second.scope.view != s.inputView) return EDITOR_REFUSED;
    if (!s.Valid(found->second.scope) || (s.NativeReady() && !s.Current(found->second.object))) {
        s.RestorePreview(found->second);
        s.drafts.erase(found);
        return EDITOR_STALE;
    }
    if (found->second.command->preview && !s.NativeReady())
        return EDITOR_REFUSED;
    if (value.type != found->second.property.type) return EDITOR_INVALID;
    std::vector<EditorField> fields;
    if (CopyEditorFields(fields, &value, 1) != EDITOR_OK || fields.size() != 1) return EDITOR_INVALID;
    if (!s.NativeReady() && !EveryField(fields, [](const auto &field) {
        return !(field.type == EDITOR_OBJECT && field.object) && !(field.type == EDITOR_OBJECT_LIST && !field.objects.empty());
    })) return EDITOR_REFUSED;
    if (s.NativeReady() && !s.CurrentValues(fields)) return EDITOR_STALE;
    const auto preview = s.Preview(found->second, fields.front(), EDITOR_PREVIEW_UPDATE);
    if (preview != EDITOR_OK) {
        s.RestorePreview(found->second);
        s.drafts.erase(found);
        return preview;
    }
    found->second.value = std::move(fields.front());
    return EDITOR_OK;
}
EditorStatus EditorApplication::FinishEdit(EditorEdit edit, bool commit, EditorOperation *operation) {
    if (operation)
        *operation = 0;
    auto &s = *m_State;
    if (!s.Thread() || s.readOnlyCallbacks || s.querying || s.notifying) return EDITOR_REFUSED;
    auto found = s.drafts.find(edit);
    if (found == s.drafts.end()) return EDITOR_STALE;
    if (s.inputView && found->second.scope.view != s.inputView) return EDITOR_REFUSED;
    if (!commit) {
        const bool restored = s.RestorePreview(found->second);
        s.drafts.erase(found);
        return restored ? EDITOR_CANCELLED : EDITOR_ERROR;
    }
    const auto &draft = found->second;
    if (!s.Valid(draft.scope)) {
        s.RestorePreview(found->second);
        s.drafts.erase(found);
        return EDITOR_STALE;
    }
    if (!s.NativeReady() || !s.writable) return EDITOR_REFUSED;
    if (!s.Current(draft.object)) {
        s.RestorePreview(found->second);
        s.drafts.erase(found);
        return EDITOR_STALE;
    }
    auto fields = s.DraftFields(draft, draft.value);
    const auto allowed = s.Check(*draft.command, draft.scope, fields, true);
    if (allowed != EDITOR_OK) {
        if (allowed == EDITOR_STALE) {
            s.RestorePreview(found->second);
            s.drafts.erase(found);
        }
        return allowed;
    }
    if (s.operations.size() >= s.completedOperations.size() + 256 || s.pending.size() >= 256)
        return EDITOR_REFUSED;
    const auto queue = [&](std::shared_ptr<State::Draft> preview = {}) {
        const auto id = s.next++;
        s.operations.emplace(id, State::Operation{EditorResponse{EDITOR_QUEUED}, true});
        s.operations.at(id).scope = draft.scope;
        try {
            s.pending.push_back({draft.command, draft.scope, std::move(fields), id, std::move(preview)});
        } catch (...) {
            s.operations.erase(id);
            throw;
        }
        if (operation)
            *operation = id;
    };
    if (draft.deferred) {
        auto owned = std::make_shared<State::Draft>(draft);
        queue(std::move(owned));
        s.drafts.erase(found);
        return EDITOR_QUEUED;
    }
    if (draft.command->commitPreview) {
        State::Draft copy = draft;
        copy.preview = nullptr;
        auto owned = std::shared_ptr<State::Draft>(new State::Draft(std::move(copy)), [&s](State::Draft *value) {
            if (value->preview)
                s.RestorePreview(*value);
            delete value;
        });
        queue(owned);
        owned->preview = found->second.preview;
        found->second.preview = nullptr;
        s.drafts.erase(found);
        return EDITOR_QUEUED;
    }
    if (!s.RestorePreview(found->second)) {
        s.drafts.erase(found);
        return EDITOR_ERROR;
    }
    queue();
    s.drafts.erase(found);
    return EDITOR_QUEUED;
}
std::string EditorApplication::CommandProvider(const std::string &id) const {
    const auto &s = *m_State;
    if (!s.Thread()) return {};
    const auto found = s.configuration.Get().commands.find(id);
    return found == s.configuration.Get().commands.end() ? std::string() : String(found->second->provider);
}
bool EditorApplication::ValueEditor(const std::string &id, EditorValueEditorDesc &editor) const {
    const auto &s = *m_State;
    if (!s.Thread()) return false;
    const auto &editors = s.drawingValues ? *s.drawingValues : s.configuration.Get().editors;
    const auto found = editors.find(id);
    if (found == editors.end()) return false;
    editor = *found->second;
    return true;
}
EditorValueEditors EditorApplication::CaptureValueEditors() const { return m_State->Thread() ? m_State->configuration.Get().editors : EditorValueEditors{}; }
uint32_t EditorApplication::DrawValue(const std::string &id, const EditorValueInput &input) {
    EditorValueEditorDesc editor{};
    return ValueEditor(id, editor) ? DrawValue(editor, input) : EDITOR_INPUT_INVALID;
}
uint32_t EditorApplication::DrawValue(const EditorValueEditorDesc &editor, const EditorValueInput &input, const EditorValueEditors *editors) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active || s.drawingEdit >= 64 || s.readOnlyCallbacks) return EDITOR_INPUT_CANCEL;
    if (!editor.draw || editor.type != input.value.type || !input.update.write) return EDITOR_INPUT_INVALID;
    const auto previous = s.drawingValues;
    if (editors) s.drawingValues = editors;
    ++s.drawing; ++s.drawingEdit;
    uint32_t action = EDITOR_INPUT_CANCEL;
    try { action = editor.draw(editor.data, Host(), &input); } catch (...) {}
    --s.drawingEdit; --s.drawing;
    s.drawingValues = previous;
    return action;
}
EditorResponse EditorApplication::ApplyEdit(const PropertyBinding &property, EditorScope scope, ObjectRef target,
                                           const EditorValue &value, const std::vector<EditorField> &context) {
    if (!m_State->Thread() || value.type != property.type) return {EDITOR_REFUSED};
    std::vector<EditorField> fields;
    if (CopyEditorFields(fields, &value, 1) != EDITOR_OK || fields.size() != 1) return {EDITOR_INVALID};
    fields.front().key = property.argument;
    fields.insert(fields.begin(), {"target", EDITOR_OBJECT, target});
    fields.insert(fields.end(), context.begin(), context.end());
    const auto values = BorrowEditorFields(fields);
    return ApplyEdit(property.command, {scope, values.data(), uint32_t(values.size()), EditorText(property.commandProvider)});
}
EditorResponse EditorApplication::ApplyEdit(const std::string &id, const EditorRequest &input) {
    auto &s = *m_State;
    EditorResponse result{EDITOR_REFUSED};
    auto request = input;
    if (!s.Thread() || !s.NativeReady() || s.readOnlyCallbacks || s.querying || s.notifying || !s.drafts.empty() ||
        !s.writable || !s.Valid(request.scope))
        return result;
    const auto routed = s.ResolveCommandView(id, request.scope);
    if (routed != EDITOR_OK) return {routed};
    auto *document = s.CurrentDocument();
    if (!document || !document->EditOpen()) return result;
    const auto selected = s.configuration.Get().commands.find(id);
    const EditorCommandDesc *command = selected == s.configuration.Get().commands.end() ? nullptr : selected->second;
    if (request.handler.size) {
        command = nullptr;
        for (const auto &module : s.modules.All()) for (const auto &candidate : module->commands)
            if (String(candidate.id) == id && String(candidate.provider) == String(request.handler)) command = &candidate;
    }
    if (!command || command->effect != EDITOR_DOCUMENT_EDIT) return result;
    std::vector<EditorField> checked;
    result.status = s.Arguments(command->parameters, command->count, request, checked);
    if (result.status != EDITOR_OK) return result;
    if (s.drawing || (!s.executing && !s.pending.empty())) {
        if (s.pending.size() >= 256 || s.operations.size() >= s.completedOperations.size() + 256)
            return {EDITOR_REFUSED};
        ++s.editing;
        result.status = s.Check(*command, request.scope, checked);
        --s.editing;
        if (result.status != EDITOR_OK)
            return result;
        const auto id = s.next++;
        s.operations.emplace(id, State::Operation{EditorResponse{EDITOR_QUEUED}, true});
        s.operations.at(id).scope = request.scope;
        s.pending.push_back({command, request.scope, std::move(checked), id, {}, 0, {}, true});
        return {EDITOR_QUEUED, {}, {}, id};
    }
    // Only document edits may join the native dialog's enclosing transaction.
    // External callers continue through execute/finishEdit and frame dispatch.
    ++s.editing;
    try { result = s.Run(*command, request.scope, checked); }
    catch (...) { result.status = EDITOR_ERROR; }
    --s.editing;
    return result;
}
uint32_t EditorApplication::DrawEdit(EditorEdit edit, EditorString label, uint32_t focus, EditorValueStyle style,
                                     float width, float height) {
    auto &s = *m_State;
    if (!s.Thread() || !s.drawing) return EDITOR_INPUT_CANCEL;
    auto found = s.drafts.find(edit);
    if (found == s.drafts.end() || !s.Valid(found->second.scope)) return EDITOR_INPUT_CANCEL;
    if (!found->second.editor)
        return EDITOR_INPUT_INVALID;
    const auto property = found->second.property;
    const std::vector<EditorField> ownedValue{found->second.value};
    const auto value = BorrowEditorFields(ownedValue);
    const auto ownedFields = found->second.fields;
    const auto fields = BorrowEditorFields(ownedFields);
    struct Update { EditorApplication *app; EditorEdit edit; } update{this, edit};
    const auto write = [](void *data, const EditorValue *value, uint32_t count) {
        const auto &update = *static_cast<Update *>(data);
        return count == 1 && value ? update.app->UpdateEdit(update.edit, *value) : EDITOR_INVALID;
    };
    const EditorValueInput input{sizeof(input),
                                 edit,
                                 label,
                                 value.values.front(),
                                 fields.data(),
                                 uint32_t(fields.values.size()),
                                 focus,
                                 {&update, write},
                                 style,
                                 width,
                                 height,
                                 EditorText(property.choices),
                                 {found->second.scope, nullptr, 0, EditorText(property.choicesProvider)}};
    auto choiceInput = input;
    const EditorValue choiceTarget{EditorLiteral("target"), EDITOR_OBJECT, found->second.object};
    choiceInput.choicesRequest.values = &choiceTarget; choiceInput.choicesRequest.count = 1;
    return DrawValue(*found->second.editor, choiceInput, &found->second.editors);
}
uint32_t EditorApplication::DrawProperty(const std::string &property, EditorScope scope, ObjectRef target, EditorEdit &edit, EditorString label) {
    const EditorValue argument{EditorLiteral("target"), EDITOR_OBJECT, target};
    return DrawProperty(property, {scope, &argument, 1}, edit, label);
}
uint32_t EditorApplication::DrawProperty(const std::string &property, const EditorRequest &request, EditorEdit &edit, EditorString label) {
    auto &s = *m_State;
    const auto scope = request.scope;
    std::vector<EditorField> arguments;
    if (CopyEditorFields(arguments, request.values, request.count) != EDITOR_OK) return EDITOR_INPUT_INVALID;
    const auto targetField = std::find_if(arguments.begin(), arguments.end(), [](const auto &f) { return f.key == "target" && f.type == EDITOR_OBJECT; });
    if (targetField == arguments.end()) return EDITOR_INPUT_INVALID;
    const auto target = targetField->object;
    if (!s.Thread() || !s.drawing || s.readOnlyCallbacks || s.querying || s.notifying || !s.NativeReady() || !s.Valid(scope) || !s.Current(target)) return EDITOR_INPUT_INVALID;
    const auto p = s.configuration.Get().properties.find(property);
    if (p == s.configuration.Get().properties.end()) return EDITOR_INPUT_INVALID;
    auto found = s.drafts.find(edit);
    if (edit && found == s.drafts.end()) edit = 0;
    const bool owns = edit && found != s.drafts.end() && found->second.object == target &&
        found->second.property.id == property && found->second.scope.view == scope.view && found->second.arguments == arguments;
    if (edit && !owns) return EDITOR_INPUT_INVALID;
    EditorResponse projection;
    EditorField current;
    if (owns) { projection.fields = found->second.fields; current = found->second.value; }
    else {
        auto query = request; query.handler = EditorText(p->second.queryProvider);
        projection = Query(p->second.query, query);
        if (projection.status != EDITOR_OK) return EDITOR_INPUT_INVALID;
        const auto value = std::find_if(projection.fields.begin(), projection.fields.end(), [&](const auto &field) { return field.key == p->second.field; });
        if (value == projection.fields.end()) return EDITOR_INPUT_INVALID;
        current = *value;
    }
    const std::vector<EditorField> ownedValue{current};
    const auto value = BorrowEditorFields(ownedValue), fields = BorrowEditorFields(projection.fields);
    struct Update { EditorApplication *app; const std::string *property; const EditorRequest *request; ObjectRef target; EditorEdit *edit; } update{this, &property, &request, target, &edit};
    const auto write = [](void *data, const EditorValue *value, uint32_t count) {
        auto &update = *static_cast<Update *>(data);
        if (!value || count != 1) return EDITOR_INVALID;
        if (*update.edit && update.app->EditTarget(*update.edit) != update.target) return EDITOR_REFUSED;
        if (!*update.edit) {
            const auto status = update.app->BeginEdit(*update.property, *update.request, *update.edit);
            if (status != EDITOR_OK) return status;
        }
        return update.app->UpdateEdit(*update.edit, *value);
    };
    const auto &binding = owns ? found->second.property : p->second;
    const EditorValueInput input{sizeof(input), owns ? edit : 0, label, value.values.front(), fields.data(),
        uint32_t(fields.values.size()), 0, {&update, write}, EDITOR_VALUE_INLINE, 0, 0,
        EditorText(binding.choices), {scope, nullptr, 0, EditorText(binding.choicesProvider)}};
    auto choiceInput = input;
    const EditorValue choiceTarget{EditorLiteral("target"), EDITOR_OBJECT, target};
    choiceInput.choicesRequest.values = &choiceTarget; choiceInput.choicesRequest.count = 1;
    auto action = owns ? DrawValue(*s.drafts.at(edit).editor, choiceInput, &s.drafts.at(edit).editors) : DrawValue(p->second.editor, choiceInput);
    if (edit && EditTarget(edit) == target) {
        if (action & EDITOR_INPUT_CANCEL) { FinishEdit(edit, false); edit = 0; }
        else if ((action & (EDITOR_INPUT_ACCEPT | EDITOR_INPUT_DEACTIVATED)) && !(action & EDITOR_INPUT_INVALID)) {
            const auto status = FinishEdit(edit, true);
            if (status == EDITOR_QUEUED || status == EDITOR_NO_CHANGE || status == EDITOR_STALE) edit = 0;
            else action |= EDITOR_INPUT_INVALID;
        }
    }
    return action;
}
bool EditorApplication::EditState(EditorEdit edit, EditorField &value, EditorValueEditorDesc &editor) const {
    auto &s = *m_State;
    if (!s.Thread()) return false;
    const auto f = s.drafts.find(edit);
    if (f == s.drafts.end() || !s.Valid(f->second.scope)) return false;
    value = f->second.value;
    editor = f->second.editor ? *f->second.editor : EditorValueEditorDesc{};
    return true;
}
bool EditorApplication::EditState(EditorEdit edit, std::string &value, EditorValueEditorDesc &editor) const {
    EditorField field;
    if (!EditState(edit, field, editor) || field.type != EDITOR_TEXT_VALUE) return false;
    value = field.text;
    return true;
}
const PropertyBinding *EditorApplication::Property(const std::string &id) const {
    auto found = m_State->configuration.Get().properties.find(id);
    return found == m_State->configuration.Get().properties.end() ? nullptr : &found->second;
}
const std::vector<MenuEntry> &EditorApplication::Menus() const { return m_State->configuration.Get().menus; }
std::vector<EditorCommandDesc> EditorApplication::Commands(bool all) const {
    std::vector<EditorCommandDesc> result;
    if (all) {
        for (const auto &module : m_State->modules.All())
            for (const auto &d : module->commands) result.push_back(d);
    } else
        for (const auto &entry : m_State->configuration.Get().commands) result.push_back(*entry.second);
    return result;
}
std::vector<EditorToolDesc> EditorApplication::Tools(bool all) const {
    std::vector<EditorToolDesc> result;
    if (all) {
        for (const auto &module : m_State->modules.All())
            for (const auto &d : module->tools) result.push_back(d);
    } else
        for (const auto &entry : m_State->configuration.Get().tools) result.push_back(*entry.second);
    return result;
}
std::vector<EditorToolbarItemDesc> EditorApplication::ToolbarProviders() const {
    std::vector<EditorToolbarItemDesc> result;
    for (const auto &module : m_State->modules.All())
        for (const auto &d : module->toolbarItems) result.push_back(d);
    return result;
}
std::vector<EditorThemeDesc> EditorApplication::Themes(bool all) const {
    std::vector<EditorThemeDesc> result;
    if (!m_State->Thread()) return result;
    if (all) {
        for (const auto &module : m_State->modules.All())
            for (const auto &desc : module->themes) result.push_back(desc);
    } else
        for (const auto &entry : m_State->configuration.Get().themes) result.push_back(*entry.second);
    return result;
}
std::vector<EditorFontDesc> EditorApplication::Fonts(bool all) const {
    std::vector<EditorFontDesc> result;
    if (!m_State->Thread()) return result;
    if (all) {
        for (const auto &module : m_State->modules.All())
            for (const auto &desc : module->fonts) result.push_back(desc);
    } else
        for (const auto &entry : m_State->configuration.Get().fonts) result.push_back(*entry.second);
    return result;
}
std::vector<EditorImageDesc> EditorApplication::Images(bool all) const {
    std::vector<EditorImageDesc> result;
    if (!m_State->Thread()) return result;
    if (all) {
        for (const auto &module : m_State->modules.All())
            for (const auto &desc : module->images) result.push_back(desc);
    } else
        for (const auto &entry : m_State->configuration.Get().images) result.push_back(*entry.second);
    return result;
}
const std::map<std::string, EditorField> &EditorApplication::ThemeValues(const std::string &id) const {
    static const std::map<std::string, EditorField> empty;
    if (!m_State->Thread()) return empty;
    const auto found = m_State->configuration.Get().themeValues.find(id);
    return found == m_State->configuration.Get().themeValues.end() ? empty : found->second;
}
uint64_t EditorApplication::AppearanceRevision() const { return m_State->Thread() ? m_State->configuration.Get().appearanceRevision : 0; }
const std::vector<ToolbarEntry> &EditorApplication::ToolbarItems() const { return m_State->configuration.Get().toolbarItems; }
const std::map<std::string, EditorToolbarLocation> &EditorApplication::Toolbars() const { return m_State->configuration.Get().toolbars; }
EditorInputPhase EditorApplication::BindingPhase(const std::string &action) const {
    const auto found = m_State->configuration.Get().bindingPhases.find(action);
    return found == m_State->configuration.Get().bindingPhases.end() ? EDITOR_KEY_PRESSED : found->second;
}
const std::map<std::string, std::string> &EditorApplication::BindingViews() const { return m_State->configuration.Get().bindingViews; }
std::string EditorApplication::ActionView(const std::string &id) const {
    const auto found = m_State->configuration.Get().bindingViews.find(id);
    return found == m_State->configuration.Get().bindingViews.end() ? std::string{} : found->second;
}
std::string EditorApplication::ToolChoice(EditorView view) const {
    if (!m_State->Thread()) return {};
    const auto found = m_State->toolStates.find(view);
    return found == m_State->toolStates.end() ? std::string{} : found->second.choice;
}
const EditorToolDesc *EditorApplication::ActiveTool(EditorView view) const {
    auto &s = *m_State;
    if (!s.Thread()) return nullptr;
    const auto found = s.toolStates.find(view);
    if (found == s.toolStates.end()) return nullptr;
    const auto &state = found->second;
    const auto tool = s.configuration.Get().tools.find(state.choice.empty() ? state.fallback : state.choice);
    if (tool != s.configuration.Get().tools.end() && String(tool->second->view) == state.type) return tool->second;
    const auto fallback = s.configuration.Get().tools.find(state.fallback);
    return fallback == s.configuration.Get().tools.end() ? nullptr : fallback->second;
}
bool EditorApplication::ToolCaptured(EditorView view) const {
    if (!m_State->Thread()) return false;
    const auto found = m_State->toolStates.find(view);
    return found != m_State->toolStates.end() && found->second.captured;
}
EditorStatus EditorApplication::SelectTool(EditorScope scope, const std::string &id) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active || s.querying || s.checking || (s.readOnlyCallbacks && s.restoringView != scope.view))
        return EDITOR_REFUSED;
    if (!s.Valid(scope, EDITOR_WORKSPACE) || scope.document != s.document) return EDITOR_STALE;
    const auto tool = s.configuration.Get().tools.find(id);
    if (tool == s.configuration.Get().tools.end()) return EDITOR_INVALID;
    const auto source = s.toolStates.find(scope.view);
    if (s.navigation && (source == s.toolStates.end() || source->second.type != String(tool->second->view))) {
        const auto target = s.navigation->ActionView(String(tool->second->view), scope.view);
        if (!target) return EDITOR_REFUSED;
        scope = Scope(target);
    }
    if (s.readOnlyCallbacks && s.restoringView != scope.view) return EDITOR_REFUSED;
    auto found = s.toolStates.find(scope.view);
    if (found == s.toolStates.end() || found->second.type != String(tool->second->view)) return EDITOR_REFUSED;
    auto &state = found->second;
    if (state.dispatching) return EDITOR_REFUSED;
    if (state.choice == id && state.handler == tool->second && state.entered) return EDITOR_NO_CHANGE;
    if (tool->second->state && s.restoringView != scope.view) {
        ++s.checking;
        EditorStatus status = EDITOR_ERROR;
        const EditorRequest request{scope};
        try {
            status = tool->second->state(tool->second->data, Host(), &request);
        } catch (...) {
        }
        --s.checking;
        if (status != EDITOR_OK) return status;
    }
    const auto previous = state;
    if (state.entered && state.handler) {
        EditorToolInput leave{};
        leave.size = sizeof(leave);
        leave.phase = EDITOR_TOOL_LEAVE;
        ToolInput(scope, leave);
    }
    CancelTool(scope.view);
    state.choice = id;
    state.handler = tool->second;
    state.entered = false;
    EditorToolInput enter{};
    enter.size = sizeof(enter);
    enter.phase = EDITOR_TOOL_ENTER;
    const auto status = ToolInput(scope, enter);
    if (status != EDITOR_OK && status != EDITOR_NO_CHANGE) {
        // ENTER can have started a preview before failing. Restore it before
        // returning to the previous tool; no document edit may escape a failure.
        CancelTool(scope.view);
        EditorToolInput leave = enter;
        leave.phase = EDITOR_TOOL_LEAVE;
        ToolInput(scope, leave);
        state = previous;
        state.entered = false;
        state.captured = false;
        ToolInput(scope, enter);
    }
    return status;
}
void EditorApplication::RestoreTool(EditorView view, const std::string &choice) {
    auto &s = *m_State;
    if (!s.Thread()) return;
    const auto found = s.toolStates.find(view);
    if (found == s.toolStates.end()) return;
    const auto restoring = s.restoringView;
    s.restoringView = view;
    const auto status = choice.empty() ? EDITOR_NO_CHANGE : SelectTool(Scope(view), choice);
    s.restoringView = restoring;
    if (status == EDITOR_INVALID) {
        found->second.choice = choice;
        found->second.handler = nullptr;
        found->second.entered = false;
    }
}
EditorStatus EditorApplication::ToolInput(EditorScope scope, const EditorToolInput &input) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active || input.size != sizeof(input) || input.phase < EDITOR_TOOL_ENTER ||
        input.phase > EDITOR_TOOL_LEAVE || !std::isfinite(input.x) || !std::isfinite(input.y) ||
        !std::isfinite(input.dx) || !std::isfinite(input.dy) || !std::isfinite(input.wheel) ||
        !std::isfinite(input.wheelX) || !std::isfinite(input.time) || input.buttons > 7 || input.pressed > 7 ||
        input.released > 7 || input.modifiers > 15 || input.hovered > 1 || input.focused > 1)
        return EDITOR_INVALID;
    if (!s.Valid(scope, EDITOR_WORKSPACE) || scope.document != s.document) return EDITOR_STALE;
    const auto found = s.toolStates.find(scope.view);
    if (found == s.toolStates.end()) return EDITOR_NO_CHANGE;
    auto &state = found->second;
    if (state.dispatching || s.querying || s.checking || (s.readOnlyCallbacks && s.restoringView != scope.view))
        return EDITOR_REFUSED;
    const auto *current = ActiveTool(scope.view);
    if ((input.phase == EDITOR_TOOL_FRAME || input.phase == EDITOR_TOOL_ENTER) && state.handler != current) {
        if (state.entered && state.handler) {
            EditorToolInput leave = input;
            leave.phase = EDITOR_TOOL_LEAVE;
            ToolInput(scope, leave);
        }
        CancelTool(scope.view);
        state.handler = current;
        state.entered = false;
    }
    if (!state.handler) return EDITOR_NO_CHANGE;
    if (input.phase == EDITOR_TOOL_FRAME) {
        if (state.focused && !input.focused) CancelTool(scope.view);
        state.focused = input.focused != 0;
        if (!input.focused && !input.hovered && !state.captured) return EDITOR_NO_CHANGE;
    }
    if (!state.entered && input.phase == EDITOR_TOOL_FRAME) {
        auto enter = input;
        enter.phase = EDITOR_TOOL_ENTER;
        const auto status = ToolInput(scope, enter);
        if (status != EDITOR_OK && status != EDITOR_NO_CHANGE) return status;
    }
    state.dispatching = true;
    ++s.drawing;
    const auto previousInput = s.inputView;
    s.inputView = scope.view;
    uint32_t flags = 0;
    EditorStatus status = EDITOR_ERROR;
    const EditorRequest request{scope};
    auto delivered = input;
    if (input.phase == EDITOR_TOOL_FRAME && !input.hovered) {
        delivered.pressed = 0;
        delivered.wheel = delivered.wheelX = 0;
    }
    try {
        status = state.handler->input(state.handler->data, Host(), &request, &delivered, &flags);
    } catch (...) {
        status = EDITOR_ERROR;
    }
    s.inputView = previousInput;
    --s.drawing;
    state.dispatching = false;
    if (flags & ~uint32_t(EDITOR_TOOL_CAPTURED | EDITOR_TOOL_DEFAULT)) status = EDITOR_INVALID;
    if (input.phase == EDITOR_TOOL_ENTER && (status == EDITOR_OK || status == EDITOR_NO_CHANGE)) state.entered = true;
    if (input.phase == EDITOR_TOOL_LEAVE) state.entered = false;
    state.captured = input.phase == EDITOR_TOOL_FRAME && (flags & EDITOR_TOOL_CAPTURED);
    if (status != EDITOR_OK && status != EDITOR_NO_CHANGE && input.phase == EDITOR_TOOL_FRAME) CancelTool(scope.view);
    if (input.phase == EDITOR_TOOL_CANCEL) state.reset = (flags & EDITOR_TOOL_DEFAULT) != 0;
    if (input.phase == EDITOR_TOOL_FRAME && (flags & EDITOR_TOOL_DEFAULT) && !state.fallback.empty() &&
        state.choice != state.fallback)
        RestoreTool(scope.view, state.fallback);
    return status;
}
void EditorApplication::CancelTool(EditorView view) {
    auto &s = *m_State;
    if (!s.Thread() || s.querying || s.checking || s.readOnlyCallbacks) return;
    const auto found = s.toolStates.find(view);
    if (found == s.toolStates.end() || found->second.dispatching) return;
    auto &state = found->second;
    if (state.handler && state.entered) {
        EditorToolInput cancel{};
        cancel.size = sizeof(cancel);
        cancel.phase = EDITOR_TOOL_CANCEL;
        ToolInput(Scope(view), cancel);
    }
    state.captured = false;
    for (auto it = s.drafts.begin(); it != s.drafts.end();) {
        if (it->second.scope.view == view && it->second.toolOwned) {
            s.RestorePreview(it->second);
            it = s.drafts.erase(it);
        } else
            ++it;
    }
    if (state.reset && !state.fallback.empty()) {
        state.reset = false;
        if (state.handler && state.entered) {
            EditorToolInput leave{};
            leave.size = sizeof(leave);
            leave.phase = EDITOR_TOOL_LEAVE;
            ToolInput(Scope(view), leave);
        }
        state.choice = state.fallback;
        state.handler = nullptr;
        state.entered = false;
    }
}
const std::map<std::string, std::string> &EditorApplication::MenuLocations() const { return m_State->configuration.Get().locations; }
const std::vector<std::pair<std::string, std::string>> &EditorApplication::Bindings() const {
    return m_State->configuration.Get().bindings;
}
const std::map<std::string, std::vector<EditorKey>> &EditorApplication::Shortcuts() const { return m_State->configuration.Get().shortcuts; }
const char *EditorApplication::ActionTitle(const std::string &action) const {
    if (auto property = Property(action)) return property->label.c_str();
    const auto found = m_State->configuration.Get().commands.find(action);
    return found == m_State->configuration.Get().commands.end() ? nullptr : found->second->title.data;
}
bool EditorApplication::IsCommand(const std::string &action) const { return m_State->configuration.Get().commands.count(action) != 0; }
std::vector<EditorViewDesc> EditorApplication::Views(bool allProviders) const {
    std::vector<EditorViewDesc> result;
    if (allProviders) {
        for (const auto &module : m_State->modules.All())
            for (const auto &view : module->views) result.push_back(view);
        return result;
    }
    for (auto &v : m_State->configuration.Get().views)
        result.push_back(*v.second);
    return result;
}
std::vector<EditorValueEditorDesc> EditorApplication::ValueEditors() const {
    std::vector<EditorValueEditorDesc> result;
    for (auto &m : m_State->modules.All())
        for (auto &e : m->editors)
            result.push_back(e);
    return result;
}
void EditorApplication::EnterDraw() { ++m_State->drawing; }
void EditorApplication::LeaveDraw() { --m_State->drawing; }
void EditorApplication::EndFrame() {
    auto &s = *m_State;
    if (!s.Thread() || s.drawing || s.querying || s.executing || s.notifying)
        return;
    if (!s.pending.empty()) {
        struct PendingRun {
            State &state;
            explicit PendingRun(State &state) : state(state) { state.runningPending = true; }
            ~PendingRun() { state.runningPending = false; }
        } running(s);
        auto pending = std::move(s.pending);
        s.pending.clear();
        for (auto &p : pending) {
            if (p.afterEdit) {
                const auto parent = s.operations.find(p.afterEdit);
                if (parent == s.operations.end() ||
                    (parent->second.result.status != EDITOR_OK && parent->second.result.status != EDITOR_NO_CHANGE)) {
                    s.Complete(p.operation,
                               {parent == s.operations.end() ? EDITOR_STALE : parent->second.result.status});
                    continue;
                }
                for (auto &field : p.fields)
                    if (field.key == p.revisionArgument)
                        field.integer = parent->second.result.documentRevision;
            }
            if (p.preview && p.preview->deferred) {
                const auto status = s.RefreshDraft(*p.preview);
                if (status != EDITOR_OK) {
                    s.Complete(p.operation, {status});
                    continue;
                }
                p.fields = s.DraftFields(*p.preview, p.preview->value);
            }
            if (p.joinsEdit) {
                auto *doc = s.CurrentDocument();
                if (!doc || doc->Token() != p.scope.document || !doc->EditOpen()) {
                    s.Complete(p.operation, {EDITOR_STALE});
                    continue;
                }
                ++s.editing;
            }
            auto result = s.Run(*p.command, p.scope, p.fields, p.operation, p.preview.get());
            if (p.joinsEdit)
                --s.editing;
            if (report)
                report(result);
        }
    }
    for (auto it = s.drafts.begin(); it != s.drafts.end();) {
        if (!it->second.deferred) {
            ++it;
            continue;
        }
        const auto status = s.RefreshDraft(it->second);
        if (status != EDITOR_OK || s.Preview(it->second, it->second.value, EDITOR_PREVIEW_UPDATE) != EDITOR_OK) {
            s.RestorePreview(it->second);
            it = s.drafts.erase(it);
        } else
            ++it;
    }
    s.RefreshDocument();
    if (s.delivered == s.published)
        return;
    s.delivered = s.published;
    std::vector<EditorSubscription> notices;
    for (auto &w : s.watches)
        notices.push_back(w.first);
    s.notifying = true;
    for (auto id : notices) {
        auto w = s.watches.find(id);
        if (w == s.watches.end() || !s.scopes.count(w->second.scope.view))
            continue;
        const auto watcher = w->second;
        const EditorChange change{watcher.scope, s.published};
        try {
            watcher.changed(watcher.data, Host(), &change);
        } catch (...) {
            s.watches.erase(id);
        }
    }
    s.notifying = false;
}
bool EditorApplication::StartModules(std::string &error) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active || s.stopped || s.modules.Stopped() || s.modules.Busy() || s.drawing || s.querying || s.executing || s.notifying) {
        error = "Modules cannot start during another callback";
        return false;
    }
    ++s.readOnlyCallbacks;
    ++s.executing;
    const bool started = s.modules.Start(Host(), error);
    --s.executing;
    --s.readOnlyCallbacks;
    if (!started) Stop();
    return started;
}
bool EditorApplication::UpdateModules(EditorModulePhase phase) {
    auto &s = *m_State;
    if (!s.Thread() || !s.active || s.drawing || s.querying || s.executing || s.notifying) return false;
    ++s.executing;
    const bool updated = s.modules.Update(Host(), phase, report);
    --s.executing;
    return updated;
}
void EditorApplication::StopModules() {
    auto &s = *m_State;
    if (!s.Thread() || s.modules.Busy()) return;
    ++s.executing;
    s.modules.Stop(Host(), report);
    --s.executing;
}
void EditorApplication::Stop() {
    if (!m_State)
        return;
    auto &s = *m_State;
    if (!s.Thread() || s.modules.Busy())
        return;
    StopModules();
    for (const auto &entry : s.toolStates) {
        CancelTool(entry.first);
        EditorToolInput leave{};
        leave.size = sizeof(leave);
        leave.phase = EDITOR_TOOL_LEAVE;
        ToolInput(Scope(entry.first), leave);
    }
    s.toolStates.clear();
    s.active = false;
    s.stopped = true;
    s.watches.clear();
    s.ClearDrafts();
    s.pending.clear();
    s.scopes.clear();
    s.cache.clear();
    s.configuration.Clear();
    s.modules.Clear();
}
} // namespace vdev
