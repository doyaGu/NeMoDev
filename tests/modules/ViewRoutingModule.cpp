// Independent view providers: this module uses only the public editor and ImGui headers.
#include "editor/Editor.h"
#include "editor/ImGuiDrawing.h"
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {
struct Module { std::map<EditorView, EditorObject> layoutCameras; };
struct SavedValue {
    std::string key, text;
    EditorValueType type{};
    EditorObject object{};
    uint64_t integer = 0;
    double real = 0;
    std::vector<EditorObject> objects;
    std::vector<uint64_t> integers;
    std::vector<double> reals;
    std::vector<std::string> texts;
    mutable std::vector<EditorString> textViews;
    explicit SavedValue(const EditorValue &value)
        : key(value.key.data, value.key.size), text(value.text.data ? value.text.data : "", value.text.size),
          type(value.type), object(value.object), integer(value.integer), real(value.real) {
        if (value.objects.count)
            objects.assign(value.objects.data, value.objects.data + value.objects.count);
        if (value.integers.count)
            integers.assign(value.integers.data, value.integers.data + value.integers.count);
        if (value.reals.count)
            reals.assign(value.reals.data, value.reals.data + value.reals.count);
        for (uint32_t i = 0; i < value.texts.count; ++i)
            texts.emplace_back(value.texts.data[i].data, value.texts.data[i].size);
    }
    EditorValue Borrow() const {
        EditorValue value{};
        value.key = {key.c_str(), uint32_t(key.size())};
        value.type = type;
        value.object = object;
        value.text = {text.c_str(), uint32_t(text.size())};
        value.integer = integer;
        value.real = real;
        value.objects = {objects.data(), uint32_t(objects.size())};
        value.integers = {integers.data(), uint32_t(integers.size())};
        value.reals = {reals.data(), uint32_t(reals.size())};
        textViews.clear();
        for (const auto &text : texts)
            textViews.push_back({text.data(), uint32_t(text.size())});
        value.texts = {textViews.data(), uint32_t(textViews.size())};
        return value;
    }
};

struct Descriptors {
    Module state;
    std::vector<EditorCommandDesc> commands;
    std::vector<EditorQueryDesc> queries;
    std::vector<EditorViewDesc> views;
};
} // namespace

extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->size != sizeof(*host) || host->version != EDITOR_VERSION ||
        !out || out->size != sizeof(*out)) return EDITOR_INVALID;
    auto *descriptors = new Descriptors;
    auto *data = &descriptors->state;
    struct SchematicWindowState {
        std::vector<SavedValue> fields;
        EditorObject target{};
    };
    EditorViewDesc schematicWindow{};
    schematicWindow.size = sizeof(schematicWindow);
    schematicWindow.imguiVersion = EDITOR_IMGUI_VERSION;
    schematicWindow.id = EDITOR_TEXT("script.schematic");
    schematicWindow.provider = EDITOR_TEXT("routing.schematic");
    schematicWindow.title = EDITOR_TEXT("External Schematic");
    schematicWindow.area = EDITOR_DOCUMENT;
    schematicWindow.stateVersion = 1;
    static const EditorParameter schematicArguments[] = {
        {EDITOR_TEXT("target"), EDITOR_OBJECT, 1}, {EDITOR_TEXT("owner"), EDITOR_OBJECT, 1},
        {EDITOR_TEXT("object"), EDITOR_OBJECT, 1}, {EDITOR_TEXT("endpoint"), EDITOR_OBJECT, 1},
        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("sub"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("pin"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("topology"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("focus-target"), EDITOR_OBJECT, 1}, {EDITOR_TEXT("background"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("reveal"), EDITOR_INTEGER_VALUE, 1}};
    schematicWindow.parameters = schematicArguments;
    schematicWindow.count = uint32_t(std::size(schematicArguments));
    static const EditorParameter schematicWindowFields[] = {
        {EDITOR_TEXT("selection"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("zoom"), EDITOR_REAL_VALUE},
        {EDITOR_TEXT("selection-count"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("selection-target"), EDITOR_OBJECT},
        {EDITOR_TEXT("action-text"), EDITOR_TEXT_VALUE}};
    schematicWindow.fields = schematicWindowFields;
    schematicWindow.fieldCount = uint32_t(std::size(schematicWindowFields));
    schematicWindow.create = [](void *, const EditorHost *, EditorView) -> void * { return new SchematicWindowState; };
    schematicWindow.destroy = [](void *data) { delete static_cast<SchematicWindowState *>(data); };
    schematicWindow.pick = [](void *, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const EditorValue objects{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
        return sink.write(sink.data, &objects, 1);
    };
    schematicWindow.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *frame) {
        EditorImGuiWindow window(*frame);
        if (window) ImGui::TextUnformatted("External Schematic navigation");
    };
    schematicWindow.navigate = [](void *data, const EditorHost *, const EditorRequest *request) {
        auto &view = *static_cast<SchematicWindowState *>(data);
        for (uint32_t i = 0; i < request->count; ++i)
            if (std::string(request->values[i].key.data, request->values[i].key.size) == "target")
                view.target = request->values[i].object;
        return EDITOR_OK;
    };
    schematicWindow.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        auto &view = *static_cast<SchematicWindowState *>(data);
        const EditorValue values[] = {
            {EDITOR_TEXT("selection"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("scripts"), EDITOR_OBJECT_LIST},
            {EDITOR_TEXT("target"), EDITOR_OBJECT, view.target}, {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},
            {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("zoom"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, 1},
            {EDITOR_TEXT("selection-count"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("selection-target"), EDITOR_OBJECT},
            {EDITOR_TEXT("action-text"), EDITOR_TEXT_VALUE}};
        return sink.write(sink.data, values, uint32_t(std::size(values)));
    };
    schematicWindow.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        auto &view = *static_cast<SchematicWindowState *>(data);
        std::vector<EditorValue> values;
        for (const auto &field : view.fields) values.push_back(field.Borrow());
        return sink.write(sink.data, values.data(), uint32_t(values.size()));
    };
    schematicWindow.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        auto &view = *static_cast<SchematicWindowState *>(data);
        view.fields.clear();
        for (uint32_t i = 0; i < request->count; ++i) view.fields.emplace_back(request->values[i]);
        return EDITOR_OK;
    };
    descriptors->views.push_back(schematicWindow);
    struct LayoutWindowState {
        Module *owner;
        EditorView id;
        std::vector<SavedValue> fields;
    };
    EditorViewDesc layoutWindow{};
    layoutWindow.size = sizeof(layoutWindow);
    layoutWindow.imguiVersion = EDITOR_IMGUI_VERSION;
    layoutWindow.id = EDITOR_TEXT("layout.viewport");
    layoutWindow.provider = EDITOR_TEXT("routing.layout");
    layoutWindow.title = EDITOR_TEXT("External 3D Layout");
    layoutWindow.area = EDITOR_DOCUMENT;
    layoutWindow.singleInstance = 0;
    layoutWindow.stateVersion = 1;
    layoutWindow.data = data;
    static const EditorParameter layoutViewFields[] = {{EDITOR_TEXT("tool"), EDITOR_INTEGER_VALUE},
                                                       {EDITOR_TEXT("started"), EDITOR_INTEGER_VALUE}};
    layoutWindow.fields = layoutViewFields;
    layoutWindow.fieldCount = 2;
    layoutWindow.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const auto &state = *static_cast<LayoutWindowState *>(data);
        uint64_t tool = 25;
        for (const auto &value : state.fields)
            if (value.key == "tool")
                tool = value.integer;
        const EditorValue values[] = {{EDITOR_TEXT("tool"), EDITOR_INTEGER_VALUE, {}, {}, tool},
                                      {EDITOR_TEXT("started"), EDITOR_INTEGER_VALUE, {}, {}, 0}};
        return sink.write(sink.data, values, 2);
    };
    layoutWindow.pick = [](void *, const EditorHost *, const EditorRequest *, EditorSink sink) {
        const EditorValue value{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
        return sink.write(sink.data, &value, 1);
    };
    layoutWindow.create = [](void *data, const EditorHost *, EditorView id) -> void * {
        auto *owner = static_cast<Module *>(data);
        owner->layoutCameras.emplace(id, EditorObject{});
        return new LayoutWindowState{owner, id};
    };
    layoutWindow.destroy = [](void *data) {
        auto *view = static_cast<LayoutWindowState *>(data);
        view->owner->layoutCameras.erase(view->id);
        delete view;
    };
    layoutWindow.draw = [](void *, const EditorHost *, EditorView, EditorViewFrame *frame) {
        EditorImGuiWindow window(*frame);
        if (window)
            ImGui::Button("External Layout Window");
    };
    layoutWindow.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        auto &state = *static_cast<LayoutWindowState *>(data);
        std::vector<EditorValue> values;
        for (const auto &field : state.fields)
            values.push_back(field.Borrow());
        return sink.write(sink.data, values.data(), uint32_t(values.size()));
    };
    layoutWindow.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
        auto &state = *static_cast<LayoutWindowState *>(data);
        state.fields.clear();
        for (uint32_t i = 0; i < request->count; ++i)
            state.fields.emplace_back(request->values[i]);
        return EDITOR_OK;
    };
    descriptors->views.push_back(layoutWindow);
    static const EditorParameter routingCamera{EDITOR_TEXT("target"), EDITOR_OBJECT};
    descriptors->commands.push_back(
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.set-camera"), EDITOR_TEXT("routing.layout"),
         EDITOR_TEXT("Set External View Camera"), &routingCamera, 1,
         [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
             auto &cameras = static_cast<Module *>(data)->layoutCameras;
             const auto found = cameras.find(request->scope.view);
             if (found == cameras.end()) return EDITOR_STALE;
             found->second = request->values[0].object;
             const EditorValue value{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, {}, {}, found->first};
             return sink.write(sink.data, &value, 1);
         }, data, EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE});
    descriptors->commands.back().view = EDITOR_TEXT("layout.viewport");
    static const EditorParameter cameraField{EDITOR_TEXT("camera"), EDITOR_OBJECT};
    descriptors->queries.push_back(
        {sizeof(EditorQueryDesc), EDITOR_TEXT("routing.layout-camera"), EDITOR_TEXT("routing.layout"),
         nullptr, 0,
         [](void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
             const auto &cameras = static_cast<Module *>(data)->layoutCameras;
             const auto found = cameras.find(request->scope.view);
             if (found == cameras.end()) return EDITOR_STALE;
             const EditorValue value{EDITOR_TEXT("camera"), EDITOR_OBJECT, found->second};
             return sink.write(sink.data, &value, 1);
         }, data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &cameraField, 1});
    *out = {};
    out->size = sizeof(*out);
    out->version = EDITOR_VERSION;
    out->id = EditorString EDITOR_TEXT("test.view-routing");
    out->commands = descriptors->commands.data();
    out->commandCount = uint32_t(descriptors->commands.size());
    out->queries = descriptors->queries.data();
    out->queryCount = uint32_t(descriptors->queries.size());
    out->views = descriptors->views.data();
    out->viewCount = uint32_t(descriptors->views.size());
    out->data = descriptors;
    out->destroy = [](void *p) { delete static_cast<Descriptors *>(p); };
    return EDITOR_OK;
}
