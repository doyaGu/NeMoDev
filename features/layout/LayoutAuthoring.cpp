#include "features/layout/LayoutAuthoring.h"
#include "application/ObjectSelection.h"
#include "application/Preferences.h"
#include "core/document/Document.h"
#include "core/layout/LayoutView.h"

namespace vdev {
namespace {
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
} // namespace

struct LayoutAuthoring::Data {
    EditorApplication &editor;
    std::function<Document *()> current;
    std::function<LayoutView &()> view;
    ObjectSelection &selection;
    const Preferences &preferences;
    Document *Doc(const EditorRequest &r) const {
        auto *doc = current();
        return doc && doc->IsOpen() && doc->Token() == r.scope.document ? doc : nullptr;
    }
    static EditorStatus Creation(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.Doc(*r);
        if (!doc || self.view().Context() != doc->Context())
            return EDITOR_STALE;
        auto *camera = self.view().Camera();
        const auto &p = self.preferences;
        const auto selected =
            self.selection.References().size() == 1 ? self.selection.References().front() : ObjectRef{};
        return Write(
            sink, {{"camera", EDITOR_OBJECT, camera ? doc->Reference(camera->GetID()) : ObjectRef{}},
                   {"target", EDITOR_OBJECT, doc->Resolve(selected) ? selected : ObjectRef{}},
                   {"camera-cone", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(p.Bool("3D Layout\\Auto Show Camera Cone"))},
                   {"light-cone", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(p.Bool("3D Layout\\Auto Show Light Cone"))},
                   {"show-player",
                    EDITOR_INTEGER_VALUE,
                    {},
                    {},
                    uint64_t(p.Bool("3D Layout\\Auto Show Entities In Player Mode"))},
                   {"show-setup", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(p.Bool("3D Layout\\Auto Show Setup"))}});
    }
};

LayoutAuthoring::LayoutAuthoring(EditorApplication &editor, std::function<Document *()> current,
                                 std::function<LayoutView &()> view, ObjectSelection &selection,
                                 const Preferences &preferences)
    : m_Data(std::make_unique<Data>(Data{editor, std::move(current), std::move(view), selection, preferences})) {}
LayoutAuthoring::~LayoutAuthoring() = default;
const std::vector<ProviderChoice> &LayoutAuthoring::DefaultProviders() {
    static const std::vector<ProviderChoice> providers{{"query", "layout.creation", "builtin.layout-authoring"}};
    return providers;
}
bool LayoutAuthoring::Register(std::string &error) {
    const EditorParameter creationFields[] = {{EDITOR_TEXT("camera"), EDITOR_OBJECT},
                                              {EDITOR_TEXT("target"), EDITOR_OBJECT},
                                              {EDITOR_TEXT("camera-cone"), EDITOR_INTEGER_VALUE},
                                              {EDITOR_TEXT("light-cone"), EDITOR_INTEGER_VALUE},
                                              {EDITOR_TEXT("show-player"), EDITOR_INTEGER_VALUE},
                                              {EDITOR_TEXT("show-setup"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc queries[] = {{sizeof(EditorQueryDesc), EDITOR_TEXT("layout.creation"),
                                        EDITOR_TEXT("builtin.layout-authoring"), nullptr, 0, Data::Creation,
                                        m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, creationFields, 6}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("layout.authoring");
    module.queries = queries;
    module.queryCount = 1;
    return m_Data->editor.Register(module, error);
}
} // namespace vdev
