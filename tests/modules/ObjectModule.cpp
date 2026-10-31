#include "editor/ImGuiDrawing.h"
#include "misc/cpp/imgui_stdlib.h"

#include <iterator>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const EditorString RenameId = EDITOR_TEXT("object.rename");
struct Module {
    int views = 0, playCalls = 0, integerCalls = 0, vectorCalls = 0, vectorReads = 0, structureCalls = 0, structureReads = 0, flagCalls = 0, flagReads = 0;
    bool refuseFileView = false;
    uint64_t specializedCalls[3]{}, specializedReads[3]{};
    uint64_t selectorCalls[7]{}, selectorReads[7]{}, selectorChoices=0;
    struct Selector { Module *owner = nullptr; unsigned index = 0; } selectors[7];
    uint64_t arrayWrites[2]{}, arrayReads[2]{}, arrayDraws[2]{};
    struct Array { Module *owner; unsigned kind; } arrays[2];
    EditorObject resourceTarget{};
    EditorObject schematicTarget{};
    EditorSubscription schematicWatch = 0;
    uint64_t schematicWrites = 0, schematicNotices = 0, schematicColor = 0;
    uint64_t environmentFolders = 0, environmentPaths = 0;
    uint64_t diagnosticScans = 0, diagnosticLinks = 0;
    EditorStatus schematicRead = EDITOR_STALE;
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
EditorStatus SchematicColor(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    ++static_cast<Module *>(data)->schematicWrites;
    auto nested = *request;
    nested.handler = EDITOR_TEXT("builtin.schematic");
    return host->execute(host->data, EditorString EDITOR_TEXT("schematic.script-color-set"), &nested, sink);
}
EditorStatus WatchSchematic(void *data, const EditorHost *host, const EditorRequest *request, EditorSink) {
    auto &module = *static_cast<Module *>(data);
    if (module.schematicWatch) host->unwatch(host->data, module.schematicWatch);
    module.schematicWatch = 0;
    module.schematicTarget = request->values[0].object;
    module.schematicNotices = 0;
    return host->watch(host->data, request->values[1].integer,
        [](void *data, const EditorHost *host, const EditorChange *change) {
            auto &module = *static_cast<Module *>(data);
            ++module.schematicNotices;
            const EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT, module.schematicTarget};
            const EditorRequest query{change->scope, &target, 1};
            module.schematicRead = host->query(host->data, EditorString EDITOR_TEXT("schematic.script-color-value"),
                &query, {data, [](void *data, const EditorValue *values, uint32_t count) {
                    for (uint32_t i = 0; i < count; ++i)
                        if (std::string(values[i].key.data, values[i].key.size) == "value" &&
                            values[i].type == EDITOR_REAL_LIST && values[i].reals.count == 4) {
                            const auto *color = values[i].reals.data;
                            static_cast<Module *>(data)->schematicColor =
                                uint64_t(color[0] * 255 + .5) | uint64_t(color[1] * 255 + .5) << 8 |
                                uint64_t(color[2] * 255 + .5) << 16;
                        }
                    return EDITOR_OK;
                }});
        }, data, &module.schematicWatch);
}
EditorStatus SchematicState(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
    const auto &module = *static_cast<Module *>(data);
    const EditorValue fields[] = {
        {EDITOR_TEXT("writes"), EDITOR_INTEGER_VALUE, {}, {}, module.schematicWrites},
        {EDITOR_TEXT("notices"), EDITOR_INTEGER_VALUE, {}, {}, module.schematicNotices},
        {EDITOR_TEXT("color"), EDITOR_INTEGER_VALUE, {}, {}, module.schematicColor},
        {EDITOR_TEXT("read-status"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(module.schematicRead)}};
    return sink.write(sink.data, fields, 4);
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
uint32_t DrawInteger(void *, const EditorHost *, const EditorValueInput *input) {
    std::string label(input->label.data, input->label.size);
    const float width = input->width > 0 ? input->width : ImGui::CalcItemWidth();
    ImGui::SetNextItemWidth(width > 108 ? width - 76 : 32);
    int integer = int(int64_t(input->value.integer));
    bool changed = ImGui::InputInt(label.c_str(), &integer);
    const bool active = ImGui::IsItemActive();
    bool accept = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton("Increment")) { integer += 7; changed = accept = true; }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) return EDITOR_INPUT_CANCEL;
    if (changed) {
        auto value = input->value; value.integer = uint64_t(int64_t(integer));
        if (input->update.write(input->update.data, &value, 1) != EDITOR_OK) return EDITOR_INPUT_INVALID;
    }
    return (changed ? EDITOR_INPUT_CHANGED : 0u) | (accept ? EDITOR_INPUT_ACCEPT : 0u) | (active ? EDITOR_INPUT_ACTIVE : 0u);
}
uint32_t DrawFlags(void *, const EditorHost *, const EditorValueInput *input) {
    if (input->value.type != EDITOR_INTEGER_VALUE) return EDITOR_INPUT_INVALID;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) return EDITOR_INPUT_CANCEL;
    if (ImGui::Button("Toggle external flag")) {
        auto value = input->value; value.integer ^= 1;
        return input->update.write(input->update.data, &value, 1) == EDITOR_OK ?
            EDITOR_INPUT_CHANGED | EDITOR_INPUT_ACCEPT : EDITOR_INPUT_INVALID;
    }
    return ImGui::IsItemActive() ? EDITOR_INPUT_ACTIVE : 0u;
}
const char *specializedCommandIds[] = {"parameter.curve2d-set", "parameter.dependencies-set", "parameter.lod-options-set"};
const char *specializedQueryIds[] = {"parameter.curve2d-value", "parameter.dependencies-value", "parameter.lod-options-value"};
template<unsigned Index> EditorStatus ModifySpecialized(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    ++static_cast<Module *>(data)->specializedCalls[Index];
    auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
    const auto *id = specializedCommandIds[Index];
    return host->execute(host->data, {id, uint32_t(std::char_traits<char>::length(id))}, &next, sink);
}
template<unsigned Index> EditorStatus ReadSpecialized(void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    ++static_cast<Module *>(data)->specializedReads[Index];
    auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
    const auto *id = specializedQueryIds[Index];
    return host->query(host->data, {id, uint32_t(std::char_traits<char>::length(id))}, &next, sink);
}
uint32_t DrawSpecialized(void *, const EditorHost *, const EditorValueInput *input) {
    if (input->value.type != EDITOR_RECORD) return EDITOR_INPUT_INVALID;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) return EDITOR_INPUT_CANCEL;
    std::vector<EditorValue> members(input->value.fields.data, input->value.fields.data + input->value.fields.count);
    std::vector<double> numbers;
    for (auto &member : members) {
        const std::string key(member.key.data, member.key.size);
        const char *label = key == "points" ? "Change external curve" : key == "mode" ? "Change external dependencies" :
            key == "proportions" ? "Change external LOD" : nullptr;
        if (!label) continue;
        if (ImGui::Button(label)) {
            if (key == "mode") member.integer = member.integer == 2 ? 1 : 2;
            else {
                numbers.assign(member.reals.data, member.reals.data + member.reals.count);
                if (numbers.size() <= (key == "points" ? 1u : 0u)) return EDITOR_INPUT_INVALID;
                numbers[key == "points" ? 1 : 0] += .1; member.reals = {numbers.data(), uint32_t(numbers.size())};
            }
            auto value = input->value; value.fields = {members.data(), uint32_t(members.size())};
            return input->update.write(input->update.data, &value, 1) == EDITOR_OK ? EDITOR_INPUT_CHANGED | EDITOR_INPUT_ACCEPT : EDITOR_INPUT_INVALID;
        }
        return ImGui::IsItemActive() ? EDITOR_INPUT_ACTIVE : 0u;
    }
    return EDITOR_INPUT_INVALID;
}
const char *arrayQueries[] = {"array.cells-value", "array.column-value"};
const char *arrayCommands[] = {"array.cells-set", "array.column-set"};
EditorStatus ReadArray(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink) {
    auto &a=*static_cast<Module::Array *>(data); ++a.owner->arrayReads[a.kind];
    auto next=*request; next.handler=EDITOR_TEXT("builtin.arrays");
    const char *id=arrayQueries[a.kind]; return host->query(host->data,{id,uint32_t(std::char_traits<char>::length(id))},&next,sink);
}
EditorStatus ModifyArray(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink) {
    auto &a=*static_cast<Module::Array *>(data); ++a.owner->arrayWrites[a.kind];
    auto next=*request; next.handler=EDITOR_TEXT("builtin.arrays");
    const char *id=arrayCommands[a.kind]; return host->execute(host->data,{id,uint32_t(std::char_traits<char>::length(id))},&next,sink);
}
uint32_t DrawArray(void *data,const EditorHost *,const EditorValueInput *input) {
    auto &a=*static_cast<Module::Array *>(data); ++a.owner->arrayDraws[a.kind];
    const auto original=a.kind ? input->value.fields.data[0].text : input->value.text;
    std::string text(original.data ? original.data : "",original.size);
    if (input->focus) ImGui::SetKeyboardFocusHere();
    const bool accepted=ImGui::InputText(a.kind ? "External column" : "External cell",&text,ImGuiInputTextFlags_EnterReturnsTrue);
    const bool changed=text != std::string(original.data ? original.data : "",original.size);
    auto value=input->value; std::vector<EditorValue> fields;
    if (a.kind) { fields.assign(value.fields.data,value.fields.data+value.fields.count); fields[0].text={text.data(),uint32_t(text.size())}; value.fields={fields.data(),uint32_t(fields.size())}; }
    else value.text={text.data(),uint32_t(text.size())};
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) return EDITOR_INPUT_CANCEL;
    const bool valid=!changed || input->update.write(input->update.data,&value,1)==EDITOR_OK;
    return (changed ? EDITOR_INPUT_CHANGED : 0u) | (accepted ? EDITOR_INPUT_ACCEPT : 0u) |
        (ImGui::IsItemActive() ? EDITOR_INPUT_ACTIVE : 0u) | (ImGui::IsItemDeactivatedAfterEdit() ? EDITOR_INPUT_DEACTIVATED : 0u) | (!valid ? EDITOR_INPUT_INVALID : 0u);
}
const char *selectorNames[] = {"time","key","script","animation","message","attribute","synchro"};
const char *selectorCommandIds[] = {"parameter.time-set","parameter.key-set","parameter.script-set","parameter.animation-set","parameter.message-set","parameter.attribute-set","parameter.synchro-set"};
const char *selectorQueryIds[] = {"parameter.time-value","parameter.key-value","parameter.script-value","parameter.animation-value","parameter.message-value","parameter.attribute-value","parameter.synchro-value"};
const char *selectorEditorIds[] = {"value.time","value.key","value.script","value.animation","value.message","value.attribute","value.synchro"};
EditorString SelectorId(const char *text) { return {text,uint32_t(std::char_traits<char>::length(text))}; }
EditorStatus ModifySelector(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink) {
    auto &selector=*static_cast<Module::Selector *>(data); ++selector.owner->selectorCalls[selector.index];
    auto next=*request; next.handler=EDITOR_TEXT("builtin.parameters");
    return host->execute(host->data,SelectorId(selectorCommandIds[selector.index]),&next,sink);
}
EditorStatus ReadSelector(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink) {
    auto &selector=*static_cast<Module::Selector *>(data); ++selector.owner->selectorReads[selector.index];
    auto next=*request; next.handler=EDITOR_TEXT("builtin.parameters");
    return host->query(host->data,SelectorId(selectorQueryIds[selector.index]),&next,sink);
}
EditorStatus SelectorChoices(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink) {
    ++static_cast<Module *>(data)->selectorChoices; auto next=*request; next.handler=EDITOR_TEXT("builtin.parameters");
    return host->query(host->data,EditorString EDITOR_TEXT("parameter.selection-choices"),&next,sink);
}
uint32_t DrawSelector(void *data,const EditorHost *host,const EditorValueInput *input) {
    const auto &selector=*static_cast<Module::Selector *>(data);
    if (input->choicesQuery.size) {
        EditorValue target{};
        for(uint32_t i=0;i<input->choicesRequest.count;++i)
            if(std::string(input->choicesRequest.values[i].key.data,input->choicesRequest.values[i].key.size)=="target")target=input->choicesRequest.values[i];
        const EditorRequest request{input->choicesRequest.scope,&target,1,input->choicesRequest.handler};
        const EditorSink discard{nullptr,[](void*,const EditorValue*,uint32_t){return EDITOR_OK;}};
        if(host->query(host->data,input->choicesQuery,&request,discard)!=EDITOR_OK)return EDITOR_INPUT_INVALID;
    }
    if(ImGui::IsKeyPressed(ImGuiKey_Escape))return EDITOR_INPUT_CANCEL;
    const std::string label=std::string("Change external ")+selectorNames[selector.index];
    if(ImGui::Button(label.c_str())) {
        auto value=input->value; std::vector<EditorValue> fields;
        if(value.type==EDITOR_REAL_VALUE)value.real+=1000;
        else if(value.type==EDITOR_INTEGER_VALUE)value.integer=selector.index==5?uint64_t(int64_t(-1)):value.integer+1;
        else if(value.type==EDITOR_OBJECT)value.object={};
        else if(value.type==EDITOR_RECORD) {
            fields.assign(value.fields.data,value.fields.data+value.fields.count);
            for(auto &field:fields) {
                const std::string key(field.key.data,field.key.size);
                if(key=="id")field.integer=uint64_t(int64_t(-1));
                if(key=="object")field.object={};
                if(key=="name")field.text=selector.index==4?EditorString EDITOR_TEXT("External selector message"):EditorString EDITOR_TEXT("External selector synchro");
            }
            value.fields={fields.data(),uint32_t(fields.size())};
        } else return EDITOR_INPUT_INVALID;
        return input->update.write(input->update.data,&value,1)==EDITOR_OK?EDITOR_INPUT_CHANGED|EDITOR_INPUT_ACCEPT:EDITOR_INPUT_INVALID;
    }
    return ImGui::IsItemActive()?EDITOR_INPUT_ACTIVE:0u;
}
uint32_t DrawVector(void *, const EditorHost *, const EditorValueInput *input) {
    if (input->value.type != EDITOR_REAL_LIST || input->value.reals.count != 3 || !input->value.reals.data) return EDITOR_INPUT_INVALID;
    if (ImGui::Button("Offset vector")) {
        double components[3];
        for (int i = 0; i < 3; ++i) components[i] = input->value.reals.data[i] + i + 1;
        auto value = input->value; value.reals = {components, 3};
        return input->update.write(input->update.data, &value, 1) == EDITOR_OK ?
            EDITOR_INPUT_CHANGED | EDITOR_INPUT_ACCEPT : EDITOR_INPUT_INVALID;
    }
    return ImGui::IsKeyPressed(ImGuiKey_Escape) ? EDITOR_INPUT_CANCEL : EDITOR_INPUT_NONE;
}
const EditorValue *RecordField(EditorFields fields, const char *key) {
    for (uint32_t i = 0; i < fields.count; ++i)
        if (std::string(fields.data[i].key.data, fields.data[i].key.size) == key) return &fields.data[i];
    return nullptr;
}
uint32_t DrawStructure(void *, const EditorHost *host, const EditorValueInput *input) {
    if (input->value.type != EDITOR_RECORD) return EDITOR_INPUT_INVALID;
    uint32_t action = 0;
    if (ImGui::Button("Offset nested count")) {
        auto value = input->value;
        std::deque<std::vector<EditorValue>> storage;
        const auto offset = [&](const auto &self, EditorValue &value, unsigned depth) -> bool {
            if (value.type == EDITOR_INTEGER_VALUE) { ++value.integer; return true; }
            if (value.type != EDITOR_RECORD || depth >= 32) return false;
            if (value.fields.count) storage.emplace_back(value.fields.data, value.fields.data + value.fields.count);
            else storage.emplace_back();
            auto &members = storage.back(); value.fields = {members.data(), uint32_t(members.size())};
            for (auto &member : members) if (self(self, member, depth + 1)) return true;
            return false;
        };
        if (!offset(offset, value, 0) || input->update.write(input->update.data, &value, 1) != EDITOR_OK) action |= EDITOR_INPUT_INVALID;
        else action |= EDITOR_INPUT_CHANGED | EDITOR_INPUT_ACCEPT;
    }
    const auto *schema = RecordField({input->fields, input->count}, "schema");
    if (schema && schema->type == EDITOR_RECORD) for (uint32_t i = 0; i < input->value.fields.count; ++i) {
        const auto &value = input->value.fields.data[i];
        if (value.type != EDITOR_REAL_LIST || value.reals.count != 3) continue;
        const auto key = std::string(value.key.data, value.key.size);
        const auto *metadata = RecordField(schema->fields, key.c_str());
        if (!metadata || metadata->type != EDITOR_RECORD) return EDITOR_INPUT_INVALID;
        auto child = *input; child.value = value; child.label = EDITOR_TEXT("Nested vector");
        child.fields = metadata->fields.data; child.count = metadata->fields.count; child.drawMembers = nullptr; child.membersData = nullptr;
        struct Update { const EditorValueInput *input; uint32_t index; } update{input, i};
        child.update = {&update, [](void *data, const EditorValue *value, uint32_t count) -> EditorStatus {
            const auto &u = *static_cast<Update *>(data);
            if (!value || count != 1) return EDITOR_INVALID;
            std::vector<EditorValue> members(u.input->value.fields.data, u.input->value.fields.data + u.input->value.fields.count);
            members[u.index] = *value;
            auto root = u.input->value; root.fields = {members.data(), uint32_t(members.size())};
            return u.input->update.write(u.input->update.data, &root, 1);
        }};
        action |= host->drawValue(host->data, EditorString EDITOR_TEXT("value.vector3"), &child);
        break;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) action |= EDITOR_INPUT_CANCEL;
    return action;
}
uint32_t DrawName(void *, const EditorHost *host, const EditorValueInput *input) {
    std::string text(input->value.text.data, input->value.text.size), label(input->label.data, input->label.size);
    if (input->focus)
        ImGui::SetKeyboardFocusHere();
    const bool accepted =
        ImGui::InputTextWithHint(label.c_str(), "External name editor", &text,
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    auto value = input->value; value.text = {text.data(), static_cast<uint32_t>(text.size())};
    input->update.write(input->update.data, &value, 1);
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
    static const EditorParameter integerArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}};
    static const EditorParameter vectorArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("value"), EDITOR_REAL_LIST}, {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}};
    static const EditorParameter structureArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("value"), EDITOR_RECORD},
        {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("schema"), EDITOR_RECORD}};
    static const EditorParameter colorArgs[] = {
        {EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("value"), EDITOR_REAL_LIST},
        {EDITOR_TEXT("owner"), EDITOR_OBJECT, 0, 1}, {EDITOR_TEXT("index"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("targets"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("revisions"), EDITOR_INTEGER_LIST}};
    static const EditorParameter schematicWatchArgs[] = {
        {EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("view"), EDITOR_INTEGER_VALUE}};
    static const EditorParameter environmentPaths[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("categories"), EDITOR_RECORD}};
    static const EditorParameter linkOptions[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("delay"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("prevent"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("iterations"), EDITOR_INTEGER_VALUE}};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("diagnostics.scan-versions"), EDITOR_TEXT("example.diagnostics"), EDITOR_TEXT("External Version Test"),
            nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink) { ++static_cast<Module *>(data)->diagnosticScans; return EDITOR_OK; },
            data, EDITOR_DOCUMENT, EDITOR_RUNTIME_CONTROL},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("environment.set-link-options"), EDITOR_TEXT("example.diagnostics"), EDITOR_TEXT("External Link Properties"),
            linkOptions, 4, [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
                ++static_cast<Module *>(data)->diagnosticLinks;
                auto next = *request; next.handler = EDITOR_TEXT("builtin.environment");
                return host->execute(host->data, EditorString EDITOR_TEXT("environment.set-link-options"), &next, sink);
            }, data, EDITOR_APPLICATION, EDITOR_RUNTIME_CONTROL},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("paths.choose-folder"), EDITOR_TEXT("example.environment"), EDITOR_TEXT("Choose External Folder"),
         nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
             if (++static_cast<Module *>(data)->environmentFolders == 1) return EDITOR_CANCELLED;
             const EditorValue path{EDITOR_TEXT("path"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("External chosen folder")};
             return sink.write ? sink.write(sink.data, &path, 1) : EDITOR_INVALID;
         }, data, EDITOR_APPLICATION, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("paths.apply"), EDITOR_TEXT("example.environment"), EDITOR_TEXT("Apply External Search Paths"),
         environmentPaths, 2, [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             ++static_cast<Module *>(data)->environmentPaths;
             auto next = *request; next.handler = EDITOR_TEXT("builtin.environment");
             return host->execute(host->data, EditorString EDITOR_TEXT("paths.apply"), &next, sink);
         }, data, EDITOR_APPLICATION, EDITOR_RUNTIME_CONTROL},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("schematic.script-color-set"), EDITOR_TEXT("example.schematic"),
         EDITOR_TEXT("Set Script Color"), colorArgs, 6, SchematicColor, data, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.schematic-color-refuse"), EDITOR_TEXT("example.schematic"),
         EDITOR_TEXT("Set Script Color Then Refuse"), colorArgs, 6,
         [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             const auto status = SchematicColor(data, host, request, sink);
             return status == EDITOR_OK ? EDITOR_REFUSED : status;
         }, data, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.schematic-color-throw"), EDITOR_TEXT("example.schematic"),
         EDITOR_TEXT("Set Script Color Then Throw"), colorArgs, 6,
         [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             const auto status = SchematicColor(data, host, request, sink);
             if (status == EDITOR_OK) throw std::runtime_error("Module color edit failed");
             return status;
         }, data, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.schematic-color-cancel"), EDITOR_TEXT("example.schematic"),
         EDITOR_TEXT("Set Script Color Then Return No Change"), colorArgs, 6,
         [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             const auto status = SchematicColor(data, host, request, sink);
             return status == EDITOR_OK ? EDITOR_NO_CHANGE : status;
         }, data, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("example.schematic-watch"), EDITOR_TEXT("example.schematic"),
         EDITOR_TEXT("Watch Script Changes"), schematicWatchArgs, 2, WatchSchematic, data,
         EDITOR_DOCUMENT, EDITOR_SESSION_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.flags-set"), EDITOR_TEXT("example.flags"), EDITOR_TEXT("Set External Flags"),
         integerArgs, 3, [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             ++static_cast<Module *>(data)->flagCalls;
             auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
             return host->execute(host->data, EditorString EDITOR_TEXT("parameter.flags-set"), &next, sink);
         }, data, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.structure-set"), EDITOR_TEXT("example.structure"), EDITOR_TEXT("Set External Structure"),
         structureArgs, 4, [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             ++static_cast<Module *>(data)->structureCalls;
             auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
             return host->execute(host->data, EditorString EDITOR_TEXT("parameter.structure-set"), &next, sink);
         }, data, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.vector3-set"), EDITOR_TEXT("example.vector"), EDITOR_TEXT("Set External Vector"),
         vectorArgs, 3, [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             ++static_cast<Module *>(data)->vectorCalls;
             auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
             return host->execute(host->data, EditorString EDITOR_TEXT("parameter.vector3-set"), &next, sink);
         }, data, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("parameter.integer-set"), EDITOR_TEXT("example.integer"), EDITOR_TEXT("Set External Integer"),
         integerArgs, 3, [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
             ++static_cast<Module *>(data)->integerCalls;
             auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
             return host->execute(host->data, EditorString EDITOR_TEXT("parameter.integer-set"), &next, sink);
         }, data, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT},
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
    const EditorValueEditorDesc editor{sizeof(editor),
                                      EDITOR_IMGUI_VERSION,
                                      EDITOR_TEXT("text.single-line"),
                                      EDITOR_TEXT("example.text"),
                                      EDITOR_TEXT("External text"),
                                      DrawName,
                                      data, EDITOR_TEXT_VALUE};
    // Keep descriptor arrays valid after this entry returns. The module frees
    // its own allocations at stop; the host copies strings and arrays.
    struct Descriptors {
        std::vector<EditorCommandDesc> commands;
        std::vector<EditorQueryDesc> queries;
        std::vector<EditorViewDesc> views;
        std::vector<EditorValueEditorDesc> editors;
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
    const EditorQueryDesc integerCalls{sizeof(EditorQueryDesc), EDITOR_TEXT("example.integer-count"), EDITOR_TEXT("example.integer"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue value{EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Module *>(data)->integerCalls)};
            return sink.write(sink.data, &value, 1);
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, &playCountField, 1};
    static const EditorParameter vectorFields[] = {{EDITOR_TEXT("value"), EDITOR_REAL_LIST}, {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("labels"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("values"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("scale"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("columns"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("row-labels"), EDITOR_TEXT_LIST}};
    const EditorQueryDesc vectorValue{sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.vector3-value"), EDITOR_TEXT("example.vector"),
        vectorArgs, 1, [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
            ++static_cast<Module *>(data)->vectorReads;
            auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
            return host->query(host->data, EditorString EDITOR_TEXT("parameter.vector3-value"), &next, sink);
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, vectorFields, 10};
    static const EditorParameter vectorCountFields[] = {{EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("reads"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc vectorCalls{sizeof(EditorQueryDesc), EDITOR_TEXT("example.vector-count"), EDITOR_TEXT("example.vector"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue values[] = {{EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Module *>(data)->vectorCalls)},
                {EDITOR_TEXT("reads"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Module *>(data)->vectorReads)}};
            return sink.write(sink.data, values, 2);
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, vectorCountFields, 2};
    static const EditorParameter structureFields[] = {{EDITOR_TEXT("value"), EDITOR_RECORD}, {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("labels"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("values"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("scale"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("columns"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("row-labels"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("schema"), EDITOR_RECORD}};
    const EditorQueryDesc structureValue{sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.structure-value"), EDITOR_TEXT("example.structure"),
        structureArgs, 1, [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
            ++static_cast<Module *>(data)->structureReads;
            auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
            return host->query(host->data, EditorString EDITOR_TEXT("parameter.structure-value"), &next, sink);
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, structureFields, 11};
    const EditorQueryDesc structureCount{sizeof(EditorQueryDesc), EDITOR_TEXT("example.structure-count"), EDITOR_TEXT("example.structure"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue values[] = {{EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Module *>(data)->structureCalls)},
                {EDITOR_TEXT("reads"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Module *>(data)->structureReads)}};
            return sink.write(sink.data, values, 2);
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, vectorCountFields, 2};
    static const EditorParameter flagFields[] = {{EDITOR_TEXT("value"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("labels"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("values"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("scale"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("columns"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("row-labels"), EDITOR_TEXT_LIST}};
    const EditorQueryDesc flagValue{sizeof(EditorQueryDesc), EDITOR_TEXT("parameter.flags-value"), EDITOR_TEXT("example.flags"),
        integerArgs, 1, [](void *data, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
            ++static_cast<Module *>(data)->flagReads;
            auto next = *request; next.handler = EDITOR_TEXT("builtin.parameters");
            return host->query(host->data, EditorString EDITOR_TEXT("parameter.flags-value"), &next, sink);
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, flagFields, 10};
    const EditorQueryDesc flagCount{sizeof(EditorQueryDesc), EDITOR_TEXT("example.flags-count"), EDITOR_TEXT("example.flags"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue values[] = {{EDITOR_TEXT("count"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Module *>(data)->flagCalls)},
                {EDITOR_TEXT("reads"), EDITOR_INTEGER_VALUE, {}, {}, uint64_t(static_cast<Module *>(data)->flagReads)}};
            return sink.write(sink.data, values, 2);
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, vectorCountFields, 2};
    static const EditorParameter schematicFields[] = {
        {EDITOR_TEXT("writes"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("notices"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("color"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("read-status"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc schematicState{sizeof(EditorQueryDesc), EDITOR_TEXT("example.schematic-state"),
        EDITOR_TEXT("example.schematic"), nullptr, 0, SchematicState, data, EDITOR_QUERY_EACH_READ,
        EDITOR_APPLICATION, schematicFields, 4};
    static const EditorParameter environmentFields[] = {{EDITOR_TEXT("folders"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("paths"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc environmentCounts{sizeof(EditorQueryDesc), EDITOR_TEXT("example.environment-counts"), EDITOR_TEXT("example.environment"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const auto &module = *static_cast<Module *>(data);
            const EditorValue fields[] = {{EDITOR_TEXT("folders"), EDITOR_INTEGER_VALUE, {}, {}, module.environmentFolders},
                {EDITOR_TEXT("paths"), EDITOR_INTEGER_VALUE, {}, {}, module.environmentPaths}};
            return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, environmentFields, 2};
    static const EditorParameter diagnosticPage[] = {{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1}, {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, 1}};
    static const EditorParameter diagnosticFields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("rows"), EDITOR_RECORD}, {EDITOR_TEXT("report"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("obsolete"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("missing"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("bytes"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc diagnosticReport{sizeof(EditorQueryDesc), EDITOR_TEXT("diagnostics.versions"), EDITOR_TEXT("example.diagnostics"),
        diagnosticPage, 2, [](void *, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const EditorValue fields[] = {{EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, 777}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
                {EDITOR_TEXT("next"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("rows"), EDITOR_RECORD}, {EDITOR_TEXT("report"), EDITOR_TEXT_VALUE, {}, EDITOR_TEXT("External version report")},
                {EDITOR_TEXT("obsolete"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("missing"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("bytes"), EDITOR_INTEGER_VALUE}};
            return sink.write ? sink.write(sink.data, fields, 8) : EDITOR_INVALID;
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, diagnosticFields, 8};
    static const EditorParameter diagnosticCountFields[] = {{EDITOR_TEXT("scans"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("links"), EDITOR_INTEGER_VALUE}};
    const EditorQueryDesc diagnosticCounts{sizeof(EditorQueryDesc), EDITOR_TEXT("example.diagnostic-counts"), EDITOR_TEXT("example.diagnostics"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const auto &module = *static_cast<Module *>(data);
            const EditorValue fields[] = {{EDITOR_TEXT("scans"), EDITOR_INTEGER_VALUE, {}, {}, module.diagnosticScans},
                {EDITOR_TEXT("links"), EDITOR_INTEGER_VALUE, {}, {}, module.diagnosticLinks}};
            return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, diagnosticCountFields, 2};
    auto *descriptors = new Descriptors{std::vector<EditorCommandDesc>(std::begin(commands), std::end(commands)),
                                        {query, playCount, resourceTarget, integerCalls, vectorCalls, vectorValue, structureValue, structureCount, flagValue, flagCount, schematicState, environmentCounts, diagnosticReport, diagnosticCounts},
                                        {view, fileView, outputView, logView, resourceView, resourceInputs},
                                        {editor, {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.integer"),
                                            EDITOR_TEXT("example.integer"), EDITOR_TEXT("External integer"), DrawInteger, data, EDITOR_INTEGER_VALUE},
                                        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.vector3"),
                                            EDITOR_TEXT("example.vector"), EDITOR_TEXT("External vector"), DrawVector, data, EDITOR_REAL_LIST},
                                        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.structure"),
                                            EDITOR_TEXT("example.structure"), EDITOR_TEXT("External structure"), DrawStructure, data, EDITOR_RECORD},
                                        {sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, EDITOR_TEXT("value.flags"),
                                            EDITOR_TEXT("example.flags"), EDITOR_TEXT("External flags"), DrawFlags, data, EDITOR_INTEGER_VALUE}},
                                        data};
    const EditorDrawValue specializedDraw = DrawSpecialized;
    const EditorCommand modifiers[] = {ModifySpecialized<0>, ModifySpecialized<1>, ModifySpecialized<2>};
    const EditorQuery readers[] = {ReadSpecialized<0>, ReadSpecialized<1>, ReadSpecialized<2>};
    static const EditorParameter specializedArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT}, {EDITOR_TEXT("value"), EDITOR_RECORD}, {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}};
    static const EditorParameter specializedFields[] = {{EDITOR_TEXT("value"), EDITOR_RECORD}, {EDITOR_TEXT("display"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("type"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("labels"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("values"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}, {EDITOR_TEXT("scale"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("rows"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("columns"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("row-labels"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("schema"), EDITOR_RECORD}};
    const char *valueIds[] = {"value.curve2d", "value.dependencies", "value.lod-options"};
    for (unsigned i = 0; i < 3; ++i) {
        descriptors->commands.push_back({sizeof(EditorCommandDesc), {specializedCommandIds[i], uint32_t(std::char_traits<char>::length(specializedCommandIds[i]))}, EDITOR_TEXT("example.specialized"),
            EDITOR_TEXT("Set specialized value"), specializedArgs, 3, modifiers[i], data, EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT});
        descriptors->queries.push_back({sizeof(EditorQueryDesc), {specializedQueryIds[i], uint32_t(std::char_traits<char>::length(specializedQueryIds[i]))}, EDITOR_TEXT("example.specialized"),
            specializedArgs, 1, readers[i], data, EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, specializedFields, i == 1 ? 11u : 10u});
        descriptors->editors.push_back({sizeof(EditorValueEditorDesc), EDITOR_IMGUI_VERSION, {valueIds[i], uint32_t(std::char_traits<char>::length(valueIds[i]))},
            EDITOR_TEXT("example.specialized"), EDITOR_TEXT("External specialized value"), specializedDraw, data, EDITOR_RECORD});
    }
    static const EditorParameter specializedCounts[] = {{EDITOR_TEXT("count"), EDITOR_INTEGER_LIST}, {EDITOR_TEXT("reads"), EDITOR_INTEGER_LIST}};
    descriptors->queries.push_back({sizeof(EditorQueryDesc), EDITOR_TEXT("example.specialized-count"), EDITOR_TEXT("example.specialized"),
        nullptr, 0, [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            const auto &module = *static_cast<Module *>(data);
            const EditorValue fields[] = {{EDITOR_TEXT("count"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {module.specializedCalls, 3}},
                {EDITOR_TEXT("reads"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {module.specializedReads, 3}}};
            return sink.write(sink.data, fields, 2);
        }, data, EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, specializedCounts, 2});
    static EditorParameter selectorArgs[7][3]{}, selectorFields[7][10]{};
    const EditorValueType selectorTypes[]={EDITOR_REAL_VALUE,EDITOR_INTEGER_VALUE,EDITOR_OBJECT,EDITOR_OBJECT,EDITOR_RECORD,EDITOR_INTEGER_VALUE,EDITOR_RECORD};
    for(unsigned i=0;i<7;++i) {
        data->selectors[i]={data,i};
        selectorArgs[i][0]={EDITOR_TEXT("target"),EDITOR_OBJECT}; selectorArgs[i][1]={EDITOR_TEXT("value"),selectorTypes[i],0,selectorTypes[i]==EDITOR_OBJECT?1u:0u}; selectorArgs[i][2]={EDITOR_TEXT("type"),EDITOR_INTEGER_VALUE};
        for(unsigned j=0;j<10;++j)selectorFields[i][j]=specializedFields[j];
        selectorFields[i][0]=selectorArgs[i][1];
        descriptors->commands.push_back({sizeof(EditorCommandDesc),SelectorId(selectorCommandIds[i]),EDITOR_TEXT("example.selectors"),EDITOR_TEXT("Set selector"),selectorArgs[i],3,ModifySelector,&data->selectors[i],EDITOR_DOCUMENT,EDITOR_DOCUMENT_EDIT});
        descriptors->queries.push_back({sizeof(EditorQueryDesc),SelectorId(selectorQueryIds[i]),EDITOR_TEXT("example.selectors"),selectorArgs[i],1,ReadSelector,&data->selectors[i],EDITOR_QUERY_EACH_READ,EDITOR_DOCUMENT,selectorFields[i],10});
        descriptors->editors.push_back({sizeof(EditorValueEditorDesc),EDITOR_IMGUI_VERSION,SelectorId(selectorEditorIds[i]),EDITOR_TEXT("example.selectors"),EDITOR_TEXT("External selector"),DrawSelector,&data->selectors[i],selectorTypes[i]});
    }
    static const EditorParameter selectorChoiceArgs[]={{EDITOR_TEXT("target"),EDITOR_OBJECT},{EDITOR_TEXT("filters"),EDITOR_RECORD,1}};
    static const EditorParameter selectorChoiceFields[]={{EDITOR_TEXT("labels"),EDITOR_TEXT_LIST},{EDITOR_TEXT("objects"),EDITOR_OBJECT_LIST},{EDITOR_TEXT("values"),EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("categories"),EDITOR_TEXT_LIST},{EDITOR_TEXT("classes"),EDITOR_INTEGER_LIST},{EDITOR_TEXT("class-labels"),EDITOR_TEXT_LIST},{EDITOR_TEXT("owners"),EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("owner-labels"),EDITOR_TEXT_LIST},{EDITOR_TEXT("owner"),EDITOR_OBJECT},{EDITOR_TEXT("class"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("category"),EDITOR_TEXT_VALUE},{EDITOR_TEXT("users"),EDITOR_INTEGER_LIST},{EDITOR_TEXT("has-users"),EDITOR_INTEGER_VALUE}};
    descriptors->queries.push_back({sizeof(EditorQueryDesc),EDITOR_TEXT("parameter.selection-choices"),EDITOR_TEXT("example.selectors"),selectorChoiceArgs,2,SelectorChoices,data,EDITOR_QUERY_EACH_READ,EDITOR_DOCUMENT,selectorChoiceFields,13});
    static const EditorParameter selectorCounts[]={{EDITOR_TEXT("count"),EDITOR_INTEGER_LIST},{EDITOR_TEXT("reads"),EDITOR_INTEGER_LIST},{EDITOR_TEXT("choices"),EDITOR_INTEGER_VALUE}};
    descriptors->queries.push_back({sizeof(EditorQueryDesc),EDITOR_TEXT("example.selector-count"),EDITOR_TEXT("example.selectors"),nullptr,0,
        [](void *data,const EditorHost*,const EditorRequest*,EditorSink sink){const auto &module=*static_cast<Module*>(data);
            const EditorValue fields[]={{EDITOR_TEXT("count"),EDITOR_INTEGER_LIST,{},{},0,{},{module.selectorCalls,7}},
                {EDITOR_TEXT("reads"),EDITOR_INTEGER_LIST,{},{},0,{},{module.selectorReads,7}},{EDITOR_TEXT("choices"),EDITOR_INTEGER_VALUE,{},{},module.selectorChoices}};
            return sink.write(sink.data,fields,3);},data,EDITOR_QUERY_EACH_READ,EDITOR_APPLICATION,selectorCounts,3});
    static const EditorParameter arrayCellArgs[]={{EDITOR_TEXT("target"),EDITOR_OBJECT},{EDITOR_TEXT("rows"),EDITOR_INTEGER_LIST},{EDITOR_TEXT("column"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("value"),EDITOR_TEXT_VALUE},{EDITOR_TEXT("revision"),EDITOR_INTEGER_VALUE}};
    static const EditorParameter arrayColumnArgs[]={{EDITOR_TEXT("target"),EDITOR_OBJECT},{EDITOR_TEXT("column"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("value"),EDITOR_RECORD},{EDITOR_TEXT("revision"),EDITOR_INTEGER_VALUE}};
    static const EditorParameter arrayCellFields[]={{EDITOR_TEXT("value"),EDITOR_TEXT_VALUE},{EDITOR_TEXT("rows"),EDITOR_INTEGER_LIST},{EDITOR_TEXT("column"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("revision"),EDITOR_INTEGER_VALUE}};
    static const EditorParameter arrayColumnFields[]={{EDITOR_TEXT("value"),EDITOR_RECORD},{EDITOR_TEXT("column"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("revision"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("parameter-labels"),EDITOR_TEXT_LIST},{EDITOR_TEXT("parameter-types"),EDITOR_INTEGER_LIST}};
    static const EditorParameter arrayCellCommandArgs[]={{EDITOR_TEXT("target"),EDITOR_OBJECT},{EDITOR_TEXT("revision"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("rows"),EDITOR_INTEGER_LIST},{EDITOR_TEXT("column"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("value"),EDITOR_TEXT_VALUE}};
    static const EditorParameter arrayColumnCommandArgs[]={{EDITOR_TEXT("target"),EDITOR_OBJECT},{EDITOR_TEXT("revision"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("column"),EDITOR_INTEGER_VALUE},{EDITOR_TEXT("value"),EDITOR_RECORD}};
    for(unsigned i=0;i<2;++i) {
        data->arrays[i]={data,i}; const auto *args=i?arrayColumnArgs:arrayCellArgs;
        descriptors->commands.push_back({sizeof(EditorCommandDesc),{arrayCommands[i],uint32_t(std::char_traits<char>::length(arrayCommands[i]))},EDITOR_TEXT("example.arrays"),EDITOR_TEXT("Edit table"),i?arrayColumnCommandArgs:arrayCellCommandArgs,i?4u:5u,ModifyArray,&data->arrays[i],EDITOR_DOCUMENT,EDITOR_DOCUMENT_EDIT});
        descriptors->queries.push_back({sizeof(EditorQueryDesc),{arrayQueries[i],uint32_t(std::char_traits<char>::length(arrayQueries[i]))},EDITOR_TEXT("example.arrays"),args,i?2u:3u,ReadArray,&data->arrays[i],EDITOR_QUERY_EACH_READ,EDITOR_DOCUMENT,i?arrayColumnFields:arrayCellFields,i?5u:4u});
        const char *id=i?"value.array-column":"value.string";
        descriptors->editors.push_back({sizeof(EditorValueEditorDesc),EDITOR_IMGUI_VERSION,{id,uint32_t(std::char_traits<char>::length(id))},EDITOR_TEXT("example.arrays"),EDITOR_TEXT("External table value"),DrawArray,&data->arrays[i],i?EDITOR_RECORD:EDITOR_TEXT_VALUE});
    }
    static const EditorParameter arrayCounts[]={{EDITOR_TEXT("writes"),EDITOR_INTEGER_LIST},{EDITOR_TEXT("reads"),EDITOR_INTEGER_LIST},{EDITOR_TEXT("draws"),EDITOR_INTEGER_LIST}};
    descriptors->queries.push_back({sizeof(EditorQueryDesc),EDITOR_TEXT("example.array-count"),EDITOR_TEXT("example.arrays"),nullptr,0,
        [](void *data,const EditorHost *,const EditorRequest *,EditorSink sink) { auto &m=*static_cast<Module *>(data);
            const EditorValue fields[]={{EDITOR_TEXT("writes"),EDITOR_INTEGER_LIST,{},{},0,{},{m.arrayWrites,2}}, {EDITOR_TEXT("reads"),EDITOR_INTEGER_LIST,{},{},0,{},{m.arrayReads,2}}, {EDITOR_TEXT("draws"),EDITOR_INTEGER_LIST,{},{},0,{},{m.arrayDraws,2}}}; return sink.write(sink.data,fields,3); },data,EDITOR_QUERY_EACH_READ,EDITOR_APPLICATION,arrayCounts,3});
    *out = {};
    out->size = sizeof(*out);
    out->version = EDITOR_VERSION;
    out->id = EditorString EDITOR_TEXT("example.objects");
    out->dependencies = dependencies;
    out->dependencyCount = 1;
    out->commands = descriptors->commands.data();
    out->commandCount = static_cast<uint32_t>(descriptors->commands.size());
    out->queries = descriptors->queries.data();
    out->queryCount = uint32_t(descriptors->queries.size());
    out->views = descriptors->views.data();
    out->viewCount = uint32_t(descriptors->views.size());
    out->editors = descriptors->editors.data();
    out->editorCount = uint32_t(descriptors->editors.size());
    out->data = descriptors;
    out->stop = [](void *p) {
        auto *d = static_cast<Descriptors *>(p);
        Stop(d->data);
        delete d;
    };
    return EDITOR_OK;
}
