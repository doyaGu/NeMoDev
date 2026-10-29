#include "editor/ImGuiDrawing.h"
#include "misc/cpp/imgui_stdlib.h"

#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const EditorString RenameId = EDITOR_TEXT("object.rename");
struct Module {
    int views = 0, playCalls = 0;
    bool refuseFileView = false;
    EditorObject resourceTarget{};
};
struct FileView {
    uint64_t answered = 0;
};
EditorStatus FileViewState(void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
    static_cast<Module *>(data)->refuseFileView = request->values[0].integer != 0;
    return EDITOR_OK;
}
void DrawInput(void *data, const EditorHost *host, EditorView view, EditorString queryId, EditorString answerId) {
    auto &state = *static_cast<FileView *>(data);
    uint64_t input = 0;
    const EditorRequest query{host->scope(host->data, view), nullptr, 0};
    const auto copy = [](void *data, const EditorValue *values, uint32_t count) -> EditorStatus {
        for (uint32_t i = 0; i < count; ++i)
            if (std::string(values[i].key.data, values[i].key.size) == "input")
                *static_cast<uint64_t *>(data) = values[i].integer;
        return EDITOR_OK;
    };
    if (host->query(host->data, queryId, &query, {&input, copy}) != EDITOR_OK ||
        !input || input == state.answered)
        return;
    const EditorValue values[] = {{EDITOR_TEXT("input"), EDITOR_INTEGER_VALUE, {}, {}, input},
                                  {EDITOR_TEXT("choice"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("cancel")}};
    const EditorRequest answer{query.scope, values, 2};
    host->execute(host->data, answerId, &answer, {});
    state.answered = input;
}
void DrawFiles(void *data, const EditorHost *host, EditorView view, EditorViewFrame *) {
    DrawInput(data, host, view, EDITOR_TEXT("document.input"), EDITOR_TEXT("document.respond"));
}
void DrawOutputs(void *data, const EditorHost *host, EditorView view, EditorViewFrame *) {
    DrawInput(data, host, view, EDITOR_TEXT("output.input"), EDITOR_TEXT("output.respond"));
}
void DrawLog(void *, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
    EditorImGuiWindow window(*frame);
    if (!window) return;
    ImGui::TextUnformatted("External event log");
    const EditorRequest request{host->scope(host->data, id), nullptr, 0};
    const EditorSink sink{nullptr, [](void *, const EditorValue *fields, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i)
            if (std::string(fields[i].key.data, fields[i].key.size) == "texts")
                for (uint32_t j = 0; j < fields[i].texts.count; ++j) {
                    const auto &text = fields[i].texts.data[j];
                    ImGui::TextUnformatted(text.data, text.data + text.size);
                }
        return EDITOR_OK;
    }};
    host->query(host->data, EditorString EDITOR_TEXT("log.entries"), &request, sink);
    if (ImGui::Button("Clear")) host->execute(host->data, EditorString EDITOR_TEXT("log.clear"), &request, {});
}
struct View {
    Module *module;
    const EditorHost *host;
    EditorSubscription subscription = 0;
    EditorObject object{};
    std::string name;
    bool changed = true;
};
EditorStatus Rename(void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    EditorValue args[2]{};
    std::string name;
    for (uint32_t i = 0; i < request->count; ++i) {
        const auto &arg = request->values[i];
        const std::string key(arg.key.data, arg.key.size);
        if (key == "target")
            args[0] = arg;
        else if (key == "name") {
            name = "External " + std::string(arg.text.data, arg.text.size);
            args[1] = arg;
        }
    }
    args[1].text = {name.data(), static_cast<uint32_t>(name.size())};
    const EditorRequest nested{request->scope, args, 2, EDITOR_TEXT("builtin.objects")};
    return host->execute(host->data, RenameId, &nested, sink);
}
EditorStatus RenameTwice(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    auto first = Rename(nullptr, host, request, {});
    if (first != EDITOR_OK && first != EDITOR_NO_CHANGE)
        return first;
    if (data)
        throw std::runtime_error("Module rename failed");
    EditorValue args[2] = {request->values[0], request->values[1]};
    args[1].text = EditorString EDITOR_TEXT("Second name");
    const EditorRequest next{request->scope, args, 2, EDITOR_TEXT("builtin.objects")};
    return host->execute(host->data, RenameId, &next, sink);
}
EditorStatus RenameFail(void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const auto result = RenameTwice(nullptr, host, request, sink);
    return result == EDITOR_OK ? EDITOR_REFUSED : result;
}
EditorStatus CreateFrame(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    struct Output {
        EditorSink sink;
        EditorObject created{};
    } output{sink};
    const auto copy = [](void *data, const EditorValue *values, uint32_t count) -> EditorStatus {
        auto &output = *static_cast<Output *>(data);
        for (uint32_t i = 0; i < count; ++i)
            if (std::string(values[i].key.data, values[i].key.size) == "created")
                output.created = values[i].object;
        return output.sink.write ? output.sink.write(output.sink.data, values, count) : EDITOR_OK;
    };
    auto nested = *request;
    nested.handler = EditorString EDITOR_TEXT("builtin.objects");
    const auto result =
        host->execute(host->data, EditorString EDITOR_TEXT("object.create-frame3d"), &nested, {&output, copy});
    if (result != EDITOR_OK || !output.created.lifetime)
        return result;
    const EditorValue arguments[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, output.created},
                                     {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("External Frame")}};
    const EditorRequest rename{request->scope, arguments, 2, EDITOR_TEXT("builtin.objects")};
    const auto renamed = host->execute(host->data, RenameId, &rename, {});
    if (data && renamed == EDITOR_OK)
        throw std::runtime_error("Module creation failed");
    return renamed;
}
EditorStatus CreateScripts(void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    return host->execute(host->data, EditorString EDITOR_TEXT("script.create"), request, sink);
}
EditorStatus FileCommand(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    auto nested = *request;
    nested.handler = EditorString EDITOR_TEXT("builtin.documents");
    const auto *command = static_cast<const char *>(data);
    return host->execute(host->data, {command, uint32_t(std::char_traits<char>::length(command))}, &nested, sink);
}
EditorStatus ResourceCommand(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const auto *command = static_cast<const char *>(data);
    return host->execute(host->data, {command, uint32_t(std::char_traits<char>::length(command))}, request, sink);
}
EditorStatus OutputCommand(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    auto nested = *request;
    nested.handler = EditorString EDITOR_TEXT("builtin.outputs");
    const auto *command = static_cast<const char *>(data);
    return host->execute(host->data, {command, uint32_t(std::char_traits<char>::length(command))}, &nested, sink);
}
EditorStatus Play(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    auto nested = *request;
    nested.handler = EditorString EDITOR_TEXT("builtin.execution");
    const auto status = host->execute(host->data, EditorString EDITOR_TEXT("runtime.play"), &nested, sink);
    if (status == EDITOR_OK) ++static_cast<Module *>(data)->playCalls;
    return status;
}
EditorStatus PlayCount(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const EditorValue field{EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Module *>(data)->playCalls)};
    return sink.write ? sink.write(sink.data, &field, 1) : EDITOR_INVALID;
}
EditorStatus NavigateBlocks(void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    const EditorView view = request->values[0].integer;
    const EditorRequest navigation{request->scope, request->values + 1, request->count - 1};
    return host->navigateView(host->data, EditorString EDITOR_TEXT("resource.building-blocks"), view, &navigation, sink);
}
EditorStatus NavigateScene(void *,const EditorHost *host,const EditorRequest *request,EditorSink sink) {
    const auto type=request->values[0].text;
    const auto view=request->values[1].integer;
    const EditorValue values[]={{EDITOR_TEXT("target"),EDITOR_OBJECT,request->values[2].object},
        {EDITOR_TEXT("expand"),EDITOR_INTEGER_VALUE,{},{},request->values[3].integer}};
    const EditorRequest navigation{request->scope,values,2};
    return host->navigateView(host->data,type,view,&navigation,sink);
}
EditorStatus HideSceneSelection(void *,const EditorHost *host,const EditorRequest *request,EditorSink sink) {
    std::vector<EditorObject> objects;
    const auto status=host->queryView(host->data,request->values[0].integer,{&objects,[](void*data,const EditorValue*values,uint32_t count){
        for(uint32_t i=0;i<count;++i)if(std::string(values[i].key.data,values[i].key.size)=="selection"&&values[i].objects.count)
            static_cast<std::vector<EditorObject>*>(data)->assign(values[i].objects.data,values[i].objects.data+values[i].objects.count);
        return EDITOR_OK;
    }});
    if(status!=EDITOR_OK)return status;
    const EditorValue values[]={{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST,{},{},0,{objects.data(),uint32_t(objects.size())}},
        {EDITOR_TEXT("mode"),EDITOR_INTEGER_VALUE,{},{},1}};
    const EditorRequest edit{request->scope,values,2};return host->execute(host->data,EditorString EDITOR_TEXT("scene.visibility"),&edit,sink);
}
EditorStatus Count(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto count = std::to_string(static_cast<Module *>(data)->views);
    const EditorValue value{
        EDITOR_TEXT("views"), EDITOR_TEXT_VALUE, {}, {count.data(), static_cast<uint32_t>(count.size())}};
    return sink.write(sink.data, &value, 1);
}
EditorStatus Fields(void *data, const EditorValue *fields, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const auto &field = fields[i];
        if (std::string(field.key.data, field.key.size) == "name")
            static_cast<View *>(data)->name.assign(field.text.data, field.text.size);
    }
    return EDITOR_OK;
}
void Changed(void *data, const EditorHost *, const EditorChange *) { static_cast<View *>(data)->changed = true; }
void *Create(void *data, const EditorHost *host, EditorView id) {
    auto *view = new View{static_cast<Module *>(data), host};
    ++view->module->views;
    host->watch(host->data, id, Changed, view, &view->subscription);
    return view;
}
void Destroy(void *data) {
    auto *view = static_cast<View *>(data);
    view->host->unwatch(view->host->data, view->subscription);
    --view->module->views;
    delete view;
}
void Draw(void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
    EditorImGuiWindow window(*frame);
    if (!window) return;
    auto &view = *static_cast<View *>(data);
    const auto object = host->selection(host->data);
    if (view.changed || object != view.object) {
        view.changed = false;
        view.object = object;
        view.name.clear();
        const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, object, {}};
        const EditorRequest request{host->scope(host->data, id), &target, 1};
        host->query(host->data, EditorString EDITOR_TEXT("object.details"), &request, {&view, Fields});
    }
    ImGui::TextUnformatted("External object view");
    ImGui::TextUnformatted(view.name.c_str());
}
uint32_t DrawName(void *, const EditorHost *host, const EditorTextInput *input) {
    std::string text(input->value.data, input->value.size), label(input->label.data, input->label.size);
    if (input->focus)
        ImGui::SetKeyboardFocusHere();
    const bool accepted =
        ImGui::InputTextWithHint(label.c_str(), "External name editor", &text,
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    host->editValue(host->data, input->edit, {text.data(), static_cast<uint32_t>(text.size())});
    return ImGui::IsKeyPressed(ImGuiKey_Escape)
               ? EDITOR_INPUT_CANCEL
               : (accepted ? EDITOR_INPUT_ACCEPT : 0u) | (ImGui::IsItemActive() ? EDITOR_INPUT_ACTIVE : 0u);
}

struct ResourceBrowser {
    std::string library, folder, anchor;
    std::vector<std::string> selection, expanded;
};
EditorStatus ResourceState(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &view = *static_cast<ResourceBrowser *>(data);
    std::vector<EditorString> selection, expanded;
    for (const auto &path : view.selection) selection.push_back({path.data(), uint32_t(path.size())});
    for (const auto &path : view.expanded) expanded.push_back({path.data(), uint32_t(path.size())});
    const EditorValue values[] = {
        {EDITOR_TEXT("library"), EDITOR_TEXT_VALUE, {}, {view.library.data(), uint32_t(view.library.size())}},
        {EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE, {}, {view.folder.data(), uint32_t(view.folder.size())}},
        {EDITOR_TEXT("anchor"), EDITOR_TEXT_VALUE, {}, {view.anchor.data(), uint32_t(view.anchor.size())}},
        {EDITOR_TEXT("selection"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {selection.data(), uint32_t(selection.size())}},
        {EDITOR_TEXT("expanded"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {expanded.data(), uint32_t(expanded.size())}}};
    return sink.write ? sink.write(sink.data, values, 5) : EDITOR_INVALID;
}
EditorStatus RestoreResource(void *data, const EditorHost *, const EditorRequest *request) {
    ResourceBrowser next;
    for (uint32_t i = 0; i < request->count; ++i) {
        const auto &value = request->values[i];
        const std::string key(value.key.data, value.key.size);
        if (value.type == EDITOR_TEXT_VALUE && (key == "library" || key == "folder" || key == "anchor")) {
            auto &text = key == "library" ? next.library : key == "folder" ? next.folder : next.anchor;
            text.assign(value.text.data, value.text.size);
        } else if (value.type == EDITOR_TEXT_LIST && (key == "selection" || key == "expanded")) {
            auto &texts = key == "selection" ? next.selection : next.expanded;
            for (uint32_t j = 0; j < value.texts.count; ++j) texts.emplace_back(value.texts.data[j].data, value.texts.data[j].size);
        } else return EDITOR_INVALID;
    }
    *static_cast<ResourceBrowser *>(data) = std::move(next);
    return EDITOR_OK;
}
EditorStatus InspectResource(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &view = *static_cast<ResourceBrowser *>(data);
    std::vector<EditorString> selected;
    for (const auto &path : view.selection) selected.push_back({path.data(), uint32_t(path.size())});
    const EditorValue values[] = {
        {EDITOR_TEXT("library"), EDITOR_TEXT_VALUE, {}, {view.library.data(), uint32_t(view.library.size())}},
        {EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE, {}, {view.folder.data(), uint32_t(view.folder.size())}},
        {EDITOR_TEXT("selection"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {selected.data(), uint32_t(selected.size())}},
        {EDITOR_TEXT("entries"), EDITOR_TEXT_LIST}};
    return sink.write ? sink.write(sink.data, values, 4) : EDITOR_INVALID;
}
void Stop(void *data) { delete static_cast<Module *>(data); }
} // namespace
extern "C" EDITOR_MODULE_EXPORT EditorStatus EDITOR_CALL NeMoDevModule(const EditorHost *host, EditorModuleDesc *out) {
    if (!host || host->size != sizeof(*host) || host->version != EDITOR_VERSION || !out || out->size != sizeof(*out))
        return EDITOR_INVALID;
    auto *data = new Module;
    static const EditorParameter args[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                           {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE}};
    static const EditorParameter flag{EDITOR_TEXT("refuse"), EDITOR_INTEGER_VALUE};
    static const EditorParameter creation[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, 1},
                                               {EDITOR_TEXT("camera"), EDITOR_OBJECT, 1},
                                               {EDITOR_TEXT("show-cone"), EDITOR_INTEGER_VALUE, 1},
                                               {EDITOR_TEXT("show-in-player"), EDITOR_INTEGER_VALUE, 1}};
    static const EditorParameter owners{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST};
    static const EditorParameter resources[] = {{EDITOR_TEXT("paths"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST}, {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST, 1}};
    static const EditorParameter library[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1}};
    static const EditorParameter browse[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("files"), EDITOR_TEXT_LIST, 1}, {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE, 1}};
    static const EditorParameter objects[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("initial"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("hide"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("ask-options"), EDITOR_INTEGER_VALUE, 1}};
    static const EditorParameter web[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("title"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("width"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("height"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("cmo"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("preview"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("ask-options"), EDITOR_INTEGER_VALUE, 1}};
    static const EditorString dependencies[] = {EDITOR_TEXT("objects.editing")};
    static const EditorParameter sceneNavigation[]={{EDITOR_TEXT("type"),EDITOR_TEXT_VALUE},{EDITOR_TEXT("view"),EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("target"),EDITOR_OBJECT,1},{EDITOR_TEXT("expand"),EDITOR_INTEGER_VALUE,1}};
    static const EditorParameter sceneView{EDITOR_TEXT("view"),EDITOR_INTEGER_VALUE};
    static const EditorParameter blockNavigation[] = {{EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("filter"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("category"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("selection"), EDITOR_INTEGER_LIST, 1}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("resource.load"), EDITOR_TEXT("example.resource-spy"), EDITOR_TEXT("Record resource target"),
         resources, 3, [](void *data, const EditorHost *, const EditorRequest *request, EditorSink) {
             auto &target = static_cast<Module *>(data)->resourceTarget; target = {};
             for (uint32_t i = 0; i < request->count; ++i) {
                 const auto &field = request->values[i];
                 if (std::string(field.key.data, field.key.size) == "targets" && field.objects.count == 1) target = field.objects.data[0];
             }
             return EDITOR_OK;
         }, data, EDITOR_DOCUMENT, EDITOR_FILE_OPERATION},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.library.open"), EDITOR_TEXT("example.resources"), EDITOR_TEXT("Open Library"),
         library, 2, ResourceCommand, const_cast<char *>("resource.open-library"), EDITOR_WORKSPACE, EDITOR_FILE_OPERATION},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.library.browse"), EDITOR_TEXT("example.resources"), EDITOR_TEXT("Browse Library"),
         browse, 4, ResourceCommand, const_cast<char *>("resource.browse"), EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.blocks.navigate"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Navigate Building Blocks"), blockNavigation, 4, NavigateBlocks, nullptr, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("example.scene.navigate"),EDITOR_TEXT("example.objects"),EDITOR_TEXT("Navigate Scene View"),sceneNavigation,4,NavigateScene,nullptr,EDITOR_DOCUMENT,EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc),EDITOR_TEXT("example.scene.hide"),EDITOR_TEXT("example.objects"),EDITOR_TEXT("Hide Selected Objects"),&sceneView,1,HideSceneSelection,nullptr,EDITOR_DOCUMENT,EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.export-web"), EDITOR_TEXT("example.outputs"),
         EDITOR_TEXT("Create Web Page"), web, 7, OutputCommand, const_cast<char *>("document.export-web"), EDITOR_DOCUMENT, EDITOR_FILE_OPERATION},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.print"), EDITOR_TEXT("example.outputs"),
         EDITOR_TEXT("Print Schematic"), web, 1, OutputCommand, const_cast<char *>("document.print"), EDITOR_DOCUMENT, EDITOR_FILE_OPERATION},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("runtime.play"), EDITOR_TEXT("example.execution"),
         EDITOR_TEXT("External Play"), nullptr, 0, Play, data, EDITOR_DOCUMENT, EDITOR_RUNTIME_CONTROL, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.load-resources"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Load resources"), resources, 3, FileCommand, const_cast<char *>("resource.load"),
         EDITOR_DOCUMENT, EDITOR_FILE_OPERATION, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.export-objects"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Export objects"), objects, 5, FileCommand, const_cast<char *>("object.export"),
         EDITOR_DOCUMENT, EDITOR_FILE_OPERATION, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.rename"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Rename with prefix"), args, 2, Rename, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("object.rename"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Rename with prefix"), args, 2, Rename, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.rename-twice"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Rename twice"), args, 2, RenameTwice, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.rename-fail"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Rename then refuse"), args, 2, RenameFail, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.rename-throw"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Rename then throw"), args, 2, RenameTwice, data},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.file-view-state"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("File view creation"), &flag, 1, FileViewState, data, EDITOR_WORKSPACE, EDITOR_SESSION_CHANGE,
         nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("object.create-frame3d"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Create External Frame"), creation, 4, CreateFrame, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.create-scripts"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Create Scripts"), &owners, 1, CreateScripts, nullptr},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.create-frame-throw"), EDITOR_TEXT("example.objects"),
         EDITOR_TEXT("Create then throw"), creation, 4, CreateFrame, data}};
    static const EditorParameter fields[] = {{EDITOR_TEXT("views"), EDITOR_TEXT_VALUE}};
    const EditorQueryDesc query{sizeof(query),
                                EDITOR_TEXT("example.views"),
                                EDITOR_TEXT("example.objects"),
                                nullptr,
                                0,
                                Count,
                                data,
                                EDITOR_QUERY_EACH_READ,
                                EDITOR_WORKSPACE,
                                fields,
                                1};
    const EditorViewDesc view{sizeof(view),
                              EDITOR_IMGUI_VERSION,
                              EDITOR_TEXT("example.object"),
                              EDITOR_TEXT("example.objects"),
                              EDITOR_TEXT("External Object"),
                              Create,
                              Destroy,
                              Draw,
                              data};
    const EditorViewDesc fileView{sizeof(fileView),
                                  EDITOR_IMGUI_VERSION,
                                  EDITOR_TEXT("document.file-dialogs"),
                                  EDITOR_TEXT("example.files"),
                                  EDITOR_TEXT("External file dialogs"),
                                  [](void *data, const EditorHost *, EditorView) -> void * {
                                      return static_cast<Module *>(data)->refuseFileView ? nullptr : new FileView;
                                  },
                                  [](void *data) { delete static_cast<FileView *>(data); },
                                  &DrawFiles,
                                  data,
                                  EDITOR_OVERLAY_VIEW,
                                  1, EDITOR_WORKSPACE};
    auto outputView = fileView;
    outputView.id = EditorString EDITOR_TEXT("document.output-dialogs");
    outputView.provider = EditorString EDITOR_TEXT("example.outputs");
    outputView.title = EditorString EDITOR_TEXT("External output dialogs");
    outputView.draw = &DrawOutputs;
    auto logView = fileView;
    logView.id = EditorString EDITOR_TEXT("application.event-log");
    logView.provider = EditorString EDITOR_TEXT("example.log");
    logView.title = EditorString EDITOR_TEXT("External Event Log");
    logView.kind = EDITOR_WINDOW_VIEW;
    logView.draw = DrawLog;

    static const EditorParameter resourceNavigation[] = {{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, 1},
        {EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE, 1}, {EDITOR_TEXT("files"), EDITOR_TEXT_LIST, 1}};
    static const EditorParameter resourceFields[] = {{EDITOR_TEXT("library"), EDITOR_TEXT_VALUE}, {EDITOR_TEXT("folder"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("selection"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("entries"), EDITOR_TEXT_LIST}};
    EditorViewDesc resourceView{};
    resourceView.size = sizeof(resourceView); resourceView.imguiVersion = EDITOR_IMGUI_VERSION;
    resourceView.id = EDITOR_TEXT("resource.data"); resourceView.provider = EDITOR_TEXT("example.resources"); resourceView.title = EDITOR_TEXT("External Data Resource");
    resourceView.create = [](void *data, const EditorHost *, EditorView) -> void * {
        return static_cast<Module *>(data)->refuseFileView ? nullptr : new ResourceBrowser;
    };
    resourceView.destroy = [](void *data) { delete static_cast<ResourceBrowser *>(data); };
    resourceView.draw = [](void *data, const EditorHost *, EditorView, EditorViewFrame *frame) {
        EditorImGuiWindow window(*frame); if (!window) return;
        const auto &view = *static_cast<ResourceBrowser *>(data);
        ImGui::TextUnformatted("External Data Resource"); ImGui::TextUnformatted(view.library.c_str()); ImGui::TextUnformatted(view.folder.c_str());
    };
    resourceView.data = data; resourceView.area = EDITOR_WORKSPACE;
    resourceView.parameters = resourceNavigation; resourceView.count = 3;
    resourceView.navigate = [](void *, const EditorHost *, const EditorRequest *) { return EDITOR_REFUSED; };
    resourceView.stateVersion = 1; resourceView.saveState = ResourceState; resourceView.restoreState = RestoreResource;
    resourceView.inspect = InspectResource;
    resourceView.fields = resourceFields; resourceView.fieldCount = 4;
    auto resourceInputs = fileView;
    resourceInputs.id = EDITOR_TEXT("resource.file-dialogs"); resourceInputs.provider = EDITOR_TEXT("example.resource-inputs");
    resourceInputs.title = EDITOR_TEXT("External Resource dialogs");
    resourceInputs.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *) {
        DrawInput(data, host, id, EditorString EDITOR_TEXT("resource.input"), EditorString EDITOR_TEXT("resource.respond"));
    };
    const EditorTextEditorDesc editor{sizeof(editor),
                                      EDITOR_IMGUI_VERSION,
                                      EDITOR_TEXT("text.single-line"),
                                      EDITOR_TEXT("example.text"),
                                      EDITOR_TEXT("External text"),
                                      DrawName,
                                      data};
    // Keep descriptor arrays valid after this entry returns. The module frees
    // its own allocations at stop; the host copies strings and arrays.
    struct Descriptors {
        std::vector<EditorCommandDesc> commands;
        EditorQueryDesc queries[3];
        EditorViewDesc views[6];
        EditorTextEditorDesc editor;
        Module *data;
    };
    static const EditorParameter playCountField{EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE};
    const EditorQueryDesc playCount{sizeof(EditorQueryDesc), EDITOR_TEXT("example.play-count"), EDITOR_TEXT("example.execution"),
        nullptr, 0, PlayCount, data, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, &playCountField, 1};
    static const EditorParameter resourceTargetField{EDITOR_TEXT("target"), EDITOR_OBJECT};
    const EditorQueryDesc resourceTarget{sizeof(EditorQueryDesc), EDITOR_TEXT("example.resource-target"), EDITOR_TEXT("example.resources"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue value{EDITOR_TEXT("target"), EDITOR_OBJECT, static_cast<Module *>(data)->resourceTarget};
            return sink.write ? sink.write(sink.data, &value, 1) : EDITOR_INVALID;
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &resourceTargetField, 1};
    auto *descriptors = new Descriptors{std::vector<EditorCommandDesc>(std::begin(commands), std::end(commands)),
                                        {query, playCount, resourceTarget},
                                        {view, fileView, outputView, logView, resourceView, resourceInputs},
                                        editor,
                                        data};
    *out = {};
    out->size = sizeof(*out);
    out->version = EDITOR_VERSION;
    out->id = EditorString EDITOR_TEXT("example.objects");
    out->dependencies = dependencies;
    out->dependencyCount = 1;
    out->commands = descriptors->commands.data();
    out->commandCount = static_cast<uint32_t>(descriptors->commands.size());
    out->queries = descriptors->queries;
    out->queryCount = 3;
    out->views = descriptors->views;
    out->viewCount = 6;
    out->editors = &descriptors->editor;
    out->editorCount = 1;
    out->data = descriptors;
    out->stop = [](void *p) {
        auto *d = static_cast<Descriptors *>(p);
        Stop(d->data);
        delete d;
    };
    return EDITOR_OK;
}
