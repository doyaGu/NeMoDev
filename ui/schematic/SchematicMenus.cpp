// Schematic context menus after Dev 2.5: the comment, local parameter,
// Building Block, link and Script body menus follow the user's Dev
// screenshots (2D Text.cmo); item names and shortcuts come from the
// language pack ([Schematic]<Popup>), the Script header menu from the
// documentation (scripts/script_header.html). Commands the editor does not
// implement yet are shown disabled.
#ifndef NOMINMAX
#define NOMINMAX
#endif


#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <string>

#include "ui/schematic/SchematicCanvas.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include "core/interface/InterfaceChunkFormat.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/schematic/SchematicHitTest.h"
#include "core/schematic/SchematicSearch.h"
#include "core/graph/NmsCommands.h"
#include "core/graph/ScriptCommands.h"
#include "core/graph/ReplaceCommands.h"
#include "ui/schematic/Snapshots.h"
#include "core/graph/Debugger.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/FileDialogs.h"
#include "ui/app/DesktopFiles.h"
#include "ui/controls/TypeAhead.h"
#include "core/runtime/Resources.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

namespace {

void Unavailable(const char *label, const char *shortcut = nullptr) {
    ImGui::MenuItem(label, shortcut, false, false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Not available yet");
}

const char *StyleName(int style) {
    switch (ParamDisplayMode(style)) {
    case PARAM_STYLE_COLLAPSED: return "Closed";
    case PARAM_STYLE_VALUE: return "Value";
    case PARAM_STYLE_NAME: return "Name";
    case PARAM_STYLE_NAMEVALUE: return "Name and Value";
    default: return "";
    }
}

// Dev's Spacebar order for local parameter displays (library choice).
ParameterStyle NextStyle(int style) {
    switch (ParamDisplayMode(style)) {
    case PARAM_STYLE_COLLAPSED: return PARAM_STYLE_NAME;
    case PARAM_STYLE_NAME: return PARAM_STYLE_NAMEVALUE;
    case PARAM_STYLE_NAMEVALUE: return PARAM_STYLE_VALUE;
    default: return PARAM_STYLE_COLLAPSED;
    }
}

std::string ObjectText(CKObject *object) {
    if (!object) return "(none)";
    const char *name = object->GetName();
    return AnsiToUtf8(name ? name : "") + "  [" + std::to_string(object->GetID()) + ", " +
           AnsiToUtf8(CKClassIDToString(object->GetClassID()) ? CKClassIDToString(object->GetClassID()) : "?") + "]";
}

// The .nms file dialog (the CK file API takes ANSI paths).
FileDialogOptions BehaviorFileDialog(bool save, const std::string &suggestion) {
    FileDialogOptions options;
    options.save = save;
    options.filter = L"Virtools behaviors (*.nms)\0*.nms\0All files (*.*)\0*.*\0";
    options.extension = L"nms";
    options.initial = Utf8ToWide(suggestion);
    return options;
}

} // namespace

// --------------------------------------------------------------- .nms

void SchematicCanvas::SaveBehaviorsAs(const std::vector<CK_ID> &behaviors, const std::string &suggestion) {
    if (!m_State.HasDocument() || behaviors.empty()) return;
    m_State.RequestFile(BehaviorFileDialog(true, suggestion), [this, behaviors](const FileDialogResult &selection) {
        if (selection.status != FileDialogResult::Status::Selected) return;
        const std::string &path = selection.paths.front();
    CKBehavior *first = nullptr;
    for (CK_ID id : behaviors)
        if (!first) first = CKBehavior::Cast(m_State.document->Context()->GetObject(id));
    m_SaveBehaviors = behaviors;
    m_SavePath = path;
    if (!first) {
        // Dev asks for a behavior's only.
        m_SaveInfo = BehaviorInfoDialog();
        SaveBehaviorInfo();
        return;
    }
    m_SaveInfo.Open(path, first->GetName() ? first->GetName() : "", first->GetCompatibleClassID(), true);
    });
}

void SchematicCanvas::SaveBehaviorInfo() {
    const bool asked = !m_SavePath.empty() && m_SaveInfo.Path() == m_SavePath;
    if (asked) {
        const DialogAction action = m_SaveInfo.Draw();
        if (action == DialogAction::None) return;
        if (action == DialogAction::Cancel) {
            m_SaveBehaviors.clear();
            m_SavePath.clear();
            return;
        }
        if (!m_SaveInfo.Written())
            m_State.Log(LogLevel::Warning, "Behavior Information: cannot write " + BehaviorDescriptionPath(m_SavePath));
    }
    if (m_SavePath.empty() || !m_State.HasDocument()) return;
    const std::vector<CK_ID> behaviors = m_SaveBehaviors;
    const std::string path = m_SavePath;
    m_SaveBehaviors.clear();
    m_SavePath.clear();
    auto result = std::make_shared<std::pair<bool, std::string>>();
    const bool hide = asked && m_SaveInfo.HideRepresentation();
    m_State.RunRuntime("Saving behavior graphs", [this, behaviors, path, hide, result] {
        result->first = SaveBehaviorsAsNms(*m_State.document, behaviors, path, &result->second, hide);
    }, [this, behaviors, path, result](bool ok) {
        if (!ok) return;
        if (result->first)
            m_State.Log(LogLevel::Info, "Saved " + std::to_string(behaviors.size()) + " behavior(s) to " + path);
        else
            m_State.Log(LogLevel::Error, "Save As: " + result->second);
    });
}

void SchematicCanvas::SaveSelectionAsNms() {
    if (!m_State.HasDocument()) return;
    std::vector<CK_ID> behaviors;
    for (const auto &item : m_State.selection.items)
        if (item.kind == ElementKind::Behavior || item.kind == ElementKind::Script) behaviors.push_back(item.object);
    for (CK_ID header : m_State.selection.headers) behaviors.push_back(header);
    if (behaviors.empty() && m_Doc.currentScript) behaviors.push_back(m_Doc.currentScript);
    CKObject *first = behaviors.empty() ? nullptr : m_State.document->Context()->GetObject(behaviors.front());
    SaveBehaviorsAs(behaviors, first && first->GetName() ? std::string(first->GetName()) + ".nms" : std::string());
}

void SchematicCanvas::ImportBehaviorFile(CK_ID script, const Point &local) {
    ScriptView *view = ViewFor(script);
    const CK_ID graph = view ? GraphAt(*view, local) : 0;
    if (!graph) {
        m_State.Log(LogLevel::Info, "Import Behavior Graph: point at an open Script or an expanded graph");
        return;
    }
    m_State.RequestFile(BehaviorFileDialog(false, {}), [this, script, graph, local](const FileDialogResult &selection) {
        if (selection.status != FileDialogResult::Status::Selected) return;
        const std::string path = selection.paths.front();
        auto result = std::make_shared<EditResult>(EditResult::NoChange());
        m_State.RunRuntime("Importing behavior graph", [this, script, graph, path, local, result] {
            *result = ImportBehaviorGraph(*m_State.document, script, graph, path, local);
        }, [this, result](bool ok) { if (ok) Report(*result, "Import Behavior Graph"); });
    });
}

// Dev Message Explorer (H): every message type with the Building Blocks
// that send (CKBEHAVIOR_MESSAGESENDER) or wait for it
// (CKBEHAVIOR_MESSAGERECEIVER), by the value of their Message inputs.
// Double-click shows a Building Block in the Schematic.
// Dev's Invalid Link Explorer (dialog 749): the invalid links kept after
// loading, by Script and the behavior (graph) holding them; Goto shows the
// Script with the link selected, Delete and Delete All take them off.
void SchematicCanvas::LinkErrorsExplorer() {
    if (m_State.invalidLinks.empty()) m_ShowLinkErrors = false;
    if (!m_ShowLinkErrors || !m_State.HasDocument()) return;
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 units(268.0f, 158.0f);
    const float pad = 2.0f;
    ImVec2 o;
    if (!vi::BeginWindow("Invalid Link Explorer", "Invalid Link Explorer", vi::TemplateClient(units, s, pad), s, o)) {
        vi::EndWindow();
        return;
    }
    const vi::TemplateLayout l = vi::LayoutTemplate(o, units, s, pad);
    vi::TemplatePanel(l);
    CKContext *context = m_State.document->Context();
    const std::vector<InvalidLink> &links = m_State.invalidLinks;
    if (m_LinkErrorPick >= static_cast<int>(links.size())) m_LinkErrorPick = -1;
    static const std::vector<vi::ListColumn> COLUMNS{{"In Script", 184.0f * 1.0f}, {"In Behavior", 184.0f}, {"", 0.0f}};
    std::vector<vi::ListColumn> columns = COLUMNS;
    for (vi::ListColumn &column : columns) column.width *= s;
    bool twice = false;
    const int row = vi::ListView(
        "##invalidlinks", l.Min(4, 4), l.Max(4, 4, 260, 134), s, columns, static_cast<int>(links.size()),
        [&](int r, int c) {
            CKObject *object = context->GetObject(c == 0 ? links[r].script : links[r].graph);
            return c < 2 && object && object->GetName() ? AnsiToUtf8(object->GetName()) : std::string();
        },
        [&](int r) { return r == m_LinkErrorPick; }, &twice);
    if (row >= 0) m_LinkErrorPick = row;
    const bool picked = m_LinkErrorPick >= 0;
    auto show = [&](const InvalidLink &link) {
        m_State.events.Post(ShowElement{link.script, {ElementKind::Link, link.graph, link.link, link.slot}, link.graph});
    };
    if ((vi::Button("Goto", l.Min(7, 143), l.Max(7, 143, 71, 10), "Goto", picked) || (row >= 0 && twice)) && picked)
        show(links[m_LinkErrorPick]);
    if (vi::Button("Delete", l.Min(100, 143), l.Max(100, 143, 71, 10), "Delete", picked)) {
        const InvalidLink link = links[m_LinkErrorPick];
        m_LinkErrorPick = -1;
        DeleteInvalidLinks({link});
    } else if (vi::Button("Delete All", l.Min(193, 143), l.Max(193, 143, 71, 10), "Delete All", !links.empty())) {
        const std::vector<InvalidLink> all = links;
        m_LinkErrorPick = -1;
        DeleteInvalidLinks(all);
    }
    if (vi::DialogButton("Close", ImVec2(l.right - 60.0f * s, l.buttonsY), s)) m_ShowLinkErrors = false;
    vi::EndWindow();
}

void SchematicCanvas::DeleteInvalidLinks(const std::vector<InvalidLink> &links) {
    if (links.empty() || !m_State.HasDocument()) return;
    Edit edit(*m_State.document, links.size() > 1 ? "Delete Invalid Links" : "Delete Invalid Link", Edit::Marks::Scripts);
    for (const InvalidLink &link : links) edit.ScriptData(link.script); // the displays before
    const int removed = RemoveInvalidLinks(*m_State.document, links);
    Report(edit.Commit(), "Delete Invalid Link");
    if (removed > 0)
        m_State.invalidLinks =
            FindInvalidLinks(*m_State.document, m_State.preferences.Bool("Schematic\\Test Parameter Link after Loading"));
}

void SchematicCanvas::MessageExplorer() {
    if (!m_ShowMessages || !m_State.HasDocument()) return;
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Message Explorer", &m_ShowMessages)) {
        ImGui::End();
        return;
    }
    CKContext *context = m_State.document->Context();
    CKMessageManager *messages = context->GetMessageManager();
    CKParameterManager *manager = context->GetParameterManager();
    const CKParameterType messageType = manager ? manager->ParameterGuidToType(CKPGUID_MESSAGE) : -1;
    struct Use {
        CK_ID script, behavior, parent;
        bool sender;
    };
    std::map<int, std::vector<Use>> uses;
    for (const auto &info : m_State.document->Scripts()) {
        ScriptSession *session = m_State.document->Session(info.id);
        CKBehavior *script = CKBehavior::Cast(context->GetObject(info.id));
        if (!script) continue;
        std::vector<CKBehavior *> pending{script};
        while (!pending.empty()) {
            CKBehavior *behavior = pending.back();
            pending.pop_back();
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
            const CKDWORD flags = behavior->GetFlags();
            if (!(flags & (CKBEHAVIOR_MESSAGESENDER | CKBEHAVIOR_MESSAGERECEIVER))) continue;
            for (int i = 0; i < behavior->GetInputParameterCount(); ++i) {
                CKParameterIn *input = behavior->GetInputParameter(i);
                if (!input || input->GetType() != messageType) continue;
                CKParameter *source = input->GetRealSource();
                int value = -1;
                if (source) source->GetValue(&value);
                const BehaviorData *record = session ? FindRecord(session->Data(), behavior->GetID()) : nullptr;
                uses[value].push_back({info.id, behavior->GetID(), record ? record->parentId : info.id,
                                       (flags & CKBEHAVIOR_MESSAGESENDER) != 0});
            }
        }
    }
    // Dev's layout (documentation "Message Explorer"): the messages; for the
    // one selected the Building Blocks sending it and those receiving it;
    // a double click or Goto centers the Schematic on the one selected.
    const int count = messages ? messages->GetMessageTypeCount() : 0;
    auto show = [&](const Use &use) {
        m_State.events.Post(ShowElement{use.script, ElementRef{ElementKind::Behavior, use.parent, use.behavior}, use.behavior});
    };
    const float listWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x * 0.4f);
    const float height = std::max(80.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
    ImGui::BeginChild("messagelist", ImVec2(listWidth, height), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Messages");
    for (int type = 0; type < count; ++type) {
        const char *name = messages->GetMessageTypeName(type);
        auto found = uses.find(type);
        ImGui::PushID(type);
        const std::string label = AnsiToUtf8(name ? name : "") + (found != uses.end() ? "  (" + std::to_string(found->second.size()) + ")" : "");
        if (ImGui::Selectable(label.c_str(), m_MessagePick == type)) {
            m_MessagePick = type;
            m_MessageUse = 0;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float half = (height - ImGui::GetStyle().ItemSpacing.y) * 0.5f;
    auto list = [&](const char *id, const char *title, bool senders) {
        ImGui::BeginChild(id, ImVec2(0, half), ImGuiChildFlags_Borders);
        ImGui::TextDisabled("%s", title);
        auto found = uses.find(m_MessagePick);
        if (found != uses.end())
            for (const Use &use : found->second) {
                if (use.sender != senders) continue;
                CKObject *behavior = context->GetObject(use.behavior);
                const ScriptInfo *info = m_State.document->FindScript(use.script);
                ImGui::PushID(static_cast<int>(use.behavior));
                const std::string label = AnsiToUtf8(behavior && behavior->GetName() ? behavior->GetName() : "") + "   (" +
                                          (info ? AnsiToUtf8(info->name) : std::string()) + ")";
                if (ImGui::Selectable(label.c_str(), m_MessageUse == use.behavior, ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_MessageUse = use.behavior;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) show(use);
                }
                ImGui::PopID();
            }
        ImGui::EndChild();
    };
    list("senders", "Senders", true);
    list("receivers", "Receivers", false);
    ImGui::EndGroup();
    const std::vector<Use> *chosen = uses.count(m_MessagePick) ? &uses[m_MessagePick] : nullptr;
    const Use *picked = nullptr;
    if (chosen)
        for (const Use &use : *chosen)
            if (use.behavior == m_MessageUse) picked = &use;
    ImGui::BeginDisabled(!picked);
    if (ImGui::Button("Goto", ImVec2(90, 0)) && picked) show(*picked);
    ImGui::EndDisabled();
    ImGui::End();
}

// --------------------------------------------------------------- breakpoints

// The breakpoint the selection names: a behavior, a behavior link (with its
// graph), a local, a shortcut source or a behavior's parameter.
bool SchematicCanvas::SelectionBreakpoint(ExtraDataType &type, CK_ID &object, CK_ID &owner) const {
    const Selection &selection = m_State.selection;
    if (selection.items.size() != 1 || !m_State.HasDocument()) return false;
    const ElementRef &item = selection.items[0];
    owner = 0;
    switch (item.kind) {
    case ElementKind::Behavior:
        type = EXTRA_DATA_BEHAVIOR;
        object = item.object;
        return true;
    case ElementKind::Link: {
        for (const auto &view : m_Views)
            if (view.id == selection.script)
                for (const auto &link : view.model.links)
                    if (link.ref == item && link.behaviorLink) {
                        type = EXTRA_DATA_CONNECTION;
                        object = item.object;
                        owner = item.owner;
                        return true;
                    }
        return false;
    }
    case ElementKind::LocalParam:
    case ElementKind::Shortcut:
        for (const auto &view : m_Views)
            if (view.id == selection.script)
                for (const auto &param : view.model.params)
                    if (param.ref == item) {
                        type = EXTRA_DATA_PARAMETER;
                        object = param.parameter;
                        return object != 0;
                    }
        return false;
    case ElementKind::Pin: {
        CKObject *pinOwner = m_State.document->Context()->GetObject(item.object);
        CKBehavior *behavior = CKBehavior::Cast(pinOwner);
        CKParameterOperation *operation = CKParameterOperation::Cast(pinOwner);
        CKObject *parameter = nullptr;
        if (behavior && item.pin == PinKind::ParamIn && item.index < behavior->GetInputParameterCount())
            parameter = behavior->GetInputParameter(item.index);
        if (behavior && item.pin == PinKind::ParamOut && item.index < behavior->GetOutputParameterCount())
            parameter = behavior->GetOutputParameter(item.index);
        // An operation's inputs (0, 1) and output.
        if (operation && item.pin == PinKind::ParamIn)
            parameter = item.index == 0 ? operation->GetInParameter1() : item.index == 1 ? operation->GetInParameter2() : nullptr;
        if (operation && item.pin == PinKind::ParamOut) parameter = operation->GetOutParameter();
        if (!parameter) return false;
        type = EXTRA_DATA_PARAMETER;
        object = parameter->GetID();
        return true;
    }
    default:
        return false;
    }
}

void SchematicCanvas::ToggleBreakpointOfSelection() {
    ExtraDataType type;
    CK_ID object = 0, owner = 0;
    if (!SelectionBreakpoint(type, object, owner)) {
        m_State.Log(LogLevel::Info, "Cannot add breakpoint on picked item");
        return;
    }
    if (!ToggleBreakpoint(*m_State.document, m_State.selection.script, type, object, owner))
        m_State.Log(LogLevel::Warning, "Breakpoint: the Script's display is read-only");
}

void SchematicCanvas::BreakpointItems() {
    ExtraDataType type;
    CK_ID object = 0, owner = 0;
    const bool named = SelectionBreakpoint(type, object, owner);
    ScriptSession *session = named ? m_State.document->Session(m_State.selection.script) : nullptr;
    const bool present = session && FindBreakpoint(session->Data(), type, object);
    if (ImGui::MenuItem(present ? "Remove Breakpoint" : "Add Breakpoint", "F9", false, named))
        ToggleBreakpointOfSelection();
    if (present && ImGui::MenuItem("Configure Breakpoint", "Shift+F9")) OpenBreakpointConfig();
}

namespace {

// A breakpoint as the language pack names it ([Schematic]<Debug>).
std::string BreakpointLabel(CKContext *context, const ExtraData &data) {
    auto name = [&](CKObject *object) { return AnsiToUtf8(object && object->GetName() ? object->GetName() : "?"); };
    CKObject *object = context ? context->GetObject(data.id1) : nullptr;
    if (data.type == EXTRA_DATA_BEHAVIOR) return "Breakpoint on behavior <" + name(object) + ">";
    if (data.type == EXTRA_DATA_PARAMETER) return "Breakpoint on parameter <" + name(object) + ">";
    CKBehaviorLink *link = CKBehaviorLink::Cast(object);
    CKBehaviorIO *in = link ? link->GetInBehaviorIO() : nullptr, *out = link ? link->GetOutBehaviorIO() : nullptr;
    return "Breakpoint on link between <" + name(in ? in->GetOwner() : nullptr) + "> and <" +
           name(out ? out->GetOwner() : nullptr) + ">";
}

// The behavior a parameter belongs to (an input's, an output's, a local's owner).
CK_ID OwnerBehaviorOf(CKObject *parameter) {
    CKObject *owner = nullptr;
    if (CKParameterIn *in = CKParameterIn::Cast(parameter)) owner = in->GetOwner();
    else if (CKParameter *value = CKParameter::Cast(parameter)) owner = value->GetOwner();
    if (CKParameterOperation *operation = CKParameterOperation::Cast(owner)) owner = operation->GetOwner();
    return CKBehavior::Cast(owner) ? owner->GetID() : 0;
}

} // namespace

void SchematicCanvas::GoToBreakpoint(CK_ID script, const ExtraData &data) {
    if (!m_State.HasDocument()) return;
    CKContext *context = m_State.document->Context();
    CK_ID focus = 0;
    if (data.type == EXTRA_DATA_BEHAVIOR) focus = data.id1;
    else if (data.type == EXTRA_DATA_PARAMETER) focus = OwnerBehaviorOf(context->GetObject(data.id1));
    else if (CKBehaviorLink *link = CKBehaviorLink::Cast(context->GetObject(data.id1)))
        if (link->GetInBehaviorIO() && link->GetInBehaviorIO()->GetOwner()) focus = link->GetInBehaviorIO()->GetOwner()->GetID();
    m_State.events.Post(ShowElement{script, ElementRef(), focus});
}

void SchematicCanvas::PickBreakpoint() {
    m_Doc.pickBreakpoint = true;
    m_State.events.Post(FocusSchematic{});
}

void SchematicCanvas::ConfigureBreakpoints() {
    ExtraDataType type;
    CK_ID object = 0, owner = 0;
    ScriptSession *session = SelectionBreakpoint(type, object, owner) ? m_State.document->Session(m_State.selection.script) : nullptr;
    if (session && FindBreakpoint(session->Data(), type, object)) {
        LoadBreakpointConfig(m_State.selection.script, type, object);
        return;
    }
    const std::vector<Debugger::Breakpoint> all = m_State.debugger ? m_State.debugger->Breakpoints() : std::vector<Debugger::Breakpoint>();
    if (all.empty()) {
        m_State.Log(LogLevel::Info, "Configure Breakpoints: no breakpoint (F9 on a behavior, a link or a parameter)");
        return;
    }
    LoadBreakpointConfig(all.front().script, all.front().data.type, all.front().data.id1);
}

void SchematicCanvas::OpenBreakpointList() {
    if (!m_State.debugger) return;
    m_Doc.listBreakpoints = m_State.debugger->Breakpoints();
    m_Doc.listPick = -1;
    m_Doc.openBreakpointList = true;
}

namespace {
const char *const COMPARISONS[] = {"=", "<>", "= parameter", "<> parameter", "<", "<=", ">", ">=",
                                   "< parameter", "<= parameter", "> parameter", ">= parameter"};
const char *const LOGICALS[] = {"And", "Or", "Xor"};
bool ByParameter(int comparison) { return comparison == 2 || comparison == 3 || comparison >= 8; }
// <, <=, >, >= compare numbers only: a parameter of an int or float type (or derived).
bool Ordered(CKContext *context, CK_ID parameter) {
    CKObject *object = context ? context->GetObject(parameter) : nullptr;
    CKParameterManager *manager = context ? context->GetParameterManager() : nullptr;
    if (!object || !manager) return true;
    const CKGUID type = ParameterType(object);
    return manager->IsDerivedFrom(type, CKPGUID_INT) || manager->IsDerivedFrom(type, CKPGUID_FLOAT);
}
// The "parameter" comparison of a value one (Pick Reference Parameter).
int ParameterComparison(int comparison) {
    static const int BY_PARAMETER[] = {2, 3, 2, 3, 8, 9, 10, 11, 8, 9, 10, 11};
    return BY_PARAMETER[std::max(0, std::min(11, comparison))];
}
} // namespace

// Dev "Breakpoints Configuration" (Shift+F9): the selection's breakpoint.
void SchematicCanvas::OpenBreakpointConfig() {
    ExtraDataType type;
    CK_ID object = 0, owner = 0;
    if (!SelectionBreakpoint(type, object, owner)) return;
    ScriptSession *session = m_State.document->Session(m_State.selection.script);
    if (!session || !FindBreakpoint(session->Data(), type, object)) {
        m_State.Log(LogLevel::Info, "Configure Breakpoint: add the breakpoint first (F9)");
        return;
    }
    LoadBreakpointConfig(m_State.selection.script, type, object);
}

void SchematicCanvas::LoadBreakpointConfig(CK_ID script, ExtraDataType type, CK_ID object) {
    ScriptSession *session = m_State.document->Session(script);
    const ExtraData *breakpoint = session ? FindBreakpoint(session->Data(), type, object) : nullptr;
    if (!breakpoint) return;
    CKContext *context = m_State.document->Context();
    m_Doc.breakpointScript = script;
    m_Doc.breakpointType = type;
    m_Doc.breakpointObject = object;
    // The parameters a condition may compare.
    m_Doc.conditionParameters.clear();
    auto add = [&](CKObject *parameter, const std::string &prefix) {
        if (parameter)
            m_Doc.conditionParameters.emplace_back(
                parameter->GetID(), prefix + AnsiToUtf8(parameter->GetName() ? parameter->GetName() : ""));
    };
    if (type == EXTRA_DATA_PARAMETER) add(context->GetObject(object), "");
    CKBehavior *behavior = type == EXTRA_DATA_BEHAVIOR ? CKBehavior::Cast(context->GetObject(object)) : nullptr;
    if (type == EXTRA_DATA_CONNECTION)
        if (CKBehaviorLink *link = CKBehaviorLink::Cast(context->GetObject(object)))
            if (link->GetInBehaviorIO()) behavior = link->GetInBehaviorIO()->GetOwner();
    if (behavior) {
        for (int i = 0; i < behavior->GetInputParameterCount(); ++i) add(behavior->GetInputParameter(i), "pIn ");
        for (int i = 0; i < behavior->GetOutputParameterCount(); ++i) add(behavior->GetOutputParameter(i), "pOut ");
        for (int i = 0; i < behavior->GetLocalParameterCount(); ++i) add(behavior->GetLocalParameter(i), "Local ");
    }
    m_Doc.conditions.clear();
    for (const auto &condition : breakpoint->subData) {
        ConditionRow row;
        row.parameter = condition.id1;
        row.comparison = std::max(0, std::min(11, static_cast<int>(condition.GetComparisonType())));
        row.logical = std::max(0, std::min(2, static_cast<int>(condition.GetLogicalOperator())));
        if (condition.UsesParameterReference()) {
            CKObject *reference = context->GetObject(condition.id2);
            row.value = AnsiToUtf8(reference && reference->GetName() ? reference->GetName() : "");
            for (const auto &entry : m_Doc.conditionParameters)
                if (entry.first == condition.id2) row.value = entry.second;
        } else {
            row.value = AnsiToUtf8(ParameterTextFromBytes(context, ParameterType(context->GetObject(condition.id1)),
                                                          condition.buffer));
        }
        m_Doc.conditions.push_back(row);
    }
    m_Doc.conditionPick = m_Doc.conditions.empty() ? -1 : 0;
    m_Doc.openBreakpointConfig = true;
}

// Dev's Breakpoints Configuration (dialog 758): the breakpoint chosen at the
// top (each of the open Scripts'), its conditions in the list (Schematic
// Parameter, Condition, Reference Parameter: the value compared or, for a
// "parameter" condition, the parameter, Operation: how it joins the one
// before); a double click on a cell edits it. OK writes the conditions (one
// undo step); a pick closes the dialog until the parameter is clicked.
void SchematicCanvas::BreakpointConfigDialog() {
    const char *id = "###BreakpointsConfiguration";
    if (m_Doc.openBreakpointConfig) {
        ImGui::OpenPopup(id);
        m_Doc.openBreakpointConfig = false;
    }
    if (!ImGui::IsPopupOpen(id)) return;
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 units(457.0f, 143.0f);
    const float pad = 8.0f;
    ImVec2 o;
    if (!vi::BeginDialog(id, "Breakpoints Configuration", vi::TemplateClient(units, s, pad), s, o)) return;
    const vi::TemplateLayout l = vi::LayoutTemplate(o, units, s, pad);
    vi::TemplatePanel(l);
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    std::vector<ConditionRow> &rows = m_Doc.conditions;
    if (m_Doc.conditionPick >= static_cast<int>(rows.size())) m_Doc.conditionPick = -1;
    const int pick = m_Doc.conditionPick;
    auto nameOf = [&](CK_ID parameter) {
        for (const auto &entry : m_Doc.conditionParameters)
            if (entry.first == parameter) return entry.second;
        CKObject *object = context ? context->GetObject(parameter) : nullptr;
        return AnsiToUtf8(object && object->GetName() ? object->GetName() : "");
    };
    // The breakpoint configured.
    const std::vector<Debugger::Breakpoint> all = m_State.debugger ? m_State.debugger->Breakpoints() : std::vector<Debugger::Breakpoint>();
    int current = -1;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].script == m_Doc.breakpointScript && all[i].data.type == m_Doc.breakpointType &&
            all[i].data.id1 == m_Doc.breakpointObject)
            current = static_cast<int>(i);
    ExtraData shown;
    shown.type = m_Doc.breakpointType;
    shown.id1 = m_Doc.breakpointObject;
    const int chosen = vi::Combo("##breakpoint", l.Min(5, 3), l.Max(5, 3, 445, 12), BreakpointLabel(context, shown),
                                 [&] {
                                     std::vector<std::string> labels;
                                     for (const auto &breakpoint : all) labels.push_back(BreakpointLabel(context, breakpoint.data));
                                     return labels;
                                 },
                                 current);
    if (chosen >= 0 && chosen != current)
        LoadBreakpointConfig(all[chosen].script, all[chosen].data.type, all[chosen].data.id1);
    // The conditions.
    std::vector<vi::ListColumn> columns{{"Schematic Parameter", 188.0f * s}, {"Condition", 70.0f * s},
                                        {"Reference Parameter", 201.0f * s}, {"Operation", 54.0f * s}, {"", 0.0f}};
    bool twice = false;
    int column = -1;
    const int row = vi::ListView(
        "##conditions", l.Min(96, 20), l.Max(96, 20, 354, 96), s, columns, static_cast<int>(rows.size()),
        [&](int r, int c) -> std::string {
            const ConditionRow &condition = rows[r];
            switch (c) {
            case 0: return condition.parameter ? nameOf(condition.parameter) : std::string();
            case 1: return COMPARISONS[std::max(0, std::min(11, condition.comparison))];
            case 2: return condition.value;
            case 3: return r > 0 ? LOGICALS[std::max(0, std::min(2, condition.logical))] : "";
            default: return std::string();
            }
        },
        [&](int r) { return r == m_Doc.conditionPick; }, &twice, &column);
    if (row >= 0) m_Doc.conditionPick = row;
    auto editCell = [&](int r, int c) {
        if (r < 0 || c < 0 || c > 3 || (c == 3 && r == 0)) return;
        m_Doc.editRow = r;
        m_Doc.editColumn = c;
        m_Doc.editText = rows[r].value;
        ImGui::OpenPopup("##editcell");
    };
    if (row >= 0 && twice) editCell(row, column);
    // A cell's editor: the candidates, the comparisons, the operations, or
    // the value typed (a "parameter" condition: the candidates).
    if (ImGui::BeginPopup("##editcell")) {
        const int r = m_Doc.editRow, c = m_Doc.editColumn;
        if (r >= 0 && r < static_cast<int>(rows.size())) {
            ConditionRow &condition = rows[r];
            if (c == 0 || (c == 2 && ByParameter(condition.comparison))) {
                for (const auto &entry : m_Doc.conditionParameters) {
                    ImGui::PushID(static_cast<int>(entry.first));
                    if (ImGui::Selectable(entry.second.c_str(), c == 0 ? entry.first == condition.parameter : entry.second == condition.value)) {
                        if (c == 0) condition.parameter = entry.first;
                        else condition.value = entry.second;
                    }
                    ImGui::PopID();
                }
            } else if (c == 1 || c == 3) {
                const int count = c == 1 ? 12 : 3;
                const bool ordered = c != 1 || Ordered(context, condition.parameter);
                for (int i = 0; i < count; ++i) {
                    if (c == 1 && i >= 4 && !ordered) continue;
                    const bool on = c == 1 ? condition.comparison == i : condition.logical == i;
                    if (ImGui::Selectable(c == 1 ? COMPARISONS[i] : LOGICALS[i], on)) (c == 1 ? condition.comparison : condition.logical) = i;
                }
            } else {
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(200.0f * s);
                if (ImGui::InputText("##value", &m_Doc.editText, ImGuiInputTextFlags_EnterReturnsTrue)) {
                    condition.value = m_Doc.editText;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }
    // The buttons (always enabled in Dev: without a condition chosen they do nothing).
    const bool picked = pick >= 0;
    auto button = [&](const char *label, float y, bool enabled, float x = 5.0f) {
        return vi::Button(label, l.Min(x, y), l.Max(x, y, 84, 10), label, context != nullptr) && enabled;
    };
    if (button("Add Condition", 20, context != nullptr)) {
        ConditionRow added;
        if (!m_Doc.conditionParameters.empty()) added.parameter = m_Doc.conditionParameters.front().first;
        rows.push_back(added);
        m_Doc.conditionPick = static_cast<int>(rows.size()) - 1;
    }
    if (button("Remove Condition", 32, picked)) {
        rows.erase(rows.begin() + pick);
        m_Doc.conditionPick = std::min(pick, static_cast<int>(rows.size()) - 1);
    }
    bool close = false;
    if (button("Pick Schematic Parameter", 44, picked, 6.0f)) {
        m_Doc.pickConditionRow = pick;
        m_Doc.pickReference = false;
        close = true;
    }
    if (button("Edit Reference Parameter", 56, picked)) editCell(pick, 2);
    if (button("Pick Reference Parameter", 68, picked)) {
        rows[pick].comparison = ParameterComparison(rows[pick].comparison);
        m_Doc.pickConditionRow = pick;
        m_Doc.pickReference = true;
        close = true;
    }
    if (button("Go To Breakpoint", 80, context != nullptr)) GoToBreakpoint(m_Doc.breakpointScript, shown);
    auto goToParameter = [&](CK_ID parameter) {
        if (const CK_ID behavior = parameter && context ? OwnerBehaviorOf(context->GetObject(parameter)) : 0)
            m_State.events.Post(ShowElement{m_Doc.breakpointScript, ElementRef(), behavior});
    };
    if (button("Go to Schematic Parameter", 92, picked && rows[pick].parameter)) goToParameter(rows[pick].parameter);
    if (button("Go To Reference Parameter", 104, picked && ByParameter(rows[pick].comparison))) {
        for (const auto &entry : m_Doc.conditionParameters)
            if (entry.second == rows[pick].value) goToParameter(entry.first);
    }
    vi::Text(l.Min(254, 126), l.Max(254, 126, 193, 13), "Double-click on cell to edit", false, vi::CENTER);
    // OK, Cancel.
    const float right = l.right - 60.0f * s;
    const bool cancel = vi::DialogButton("Cancel", ImVec2(right, l.buttonsY), s, ImGuiKey_Escape);
    const bool ok = vi::DialogButton("OK", ImVec2(right - 70.0f * s, l.buttonsY), s);
    if (ok && context) {
        std::vector<ExtraSubData> conditions;
        bool valid = true;
        for (const auto &condition : rows) {
            if (!condition.parameter) continue;
            ExtraSubData data;
            data.id1 = condition.parameter;
            data.SetComparisonType(static_cast<ExtraDataComparisonType>(condition.comparison));
            data.SetLogicalOperator(static_cast<ExtraDataLogicalOperator>(condition.logical));
            if (data.UsesParameterReference()) {
                // The reference parameter, by name among the candidates.
                for (const auto &entry : m_Doc.conditionParameters)
                    if (entry.second == condition.value) data.id2 = entry.first;
                valid = valid && data.id2 != 0;
            } else {
                data.buffer = ParameterBytesFromText(context, ParameterType(context->GetObject(condition.parameter)),
                                                     Utf8ToAnsi(condition.value));
                valid = valid && !data.buffer.empty();
            }
            conditions.push_back(data);
        }
        if (!valid) {
            m_State.Log(LogLevel::Warning, "Configure Breakpoint: a value does not fit its parameter's type "
                                           "(a \"parameter\" condition names one of the candidates)");
        } else {
            SetBreakpointConditions(*m_State.document, m_Doc.breakpointScript, m_Doc.breakpointType,
                                    m_Doc.breakpointObject, conditions);
            close = true;
        }
    }
    if (cancel || close) ImGui::CloseCurrentPopup();
    vi::EndDialog();
}

// Dev's Breakpoints (dialog 671, the Script Debugger's Breakpoint List):
// every breakpoint; Remove Breakpoint and Remove All take them off the list,
// Ok takes them off the Scripts (one undo step); Configure opens the
// Breakpoints Configuration on one, Goto (or a double click) shows it.
void SchematicCanvas::BreakpointListDialog() {
    const char *id = "###BreakpointList";
    if (m_Doc.openBreakpointList) {
        ImGui::OpenPopup(id);
        m_Doc.openBreakpointList = false;
    }
    if (!ImGui::IsPopupOpen(id)) return;
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 units(275.0f, 167.0f);
    const float pad = 7.0f;
    ImVec2 o;
    if (!vi::BeginDialog(id, "Breakpoints", vi::TemplateClient(units, s, pad), s, o)) return;
    const vi::TemplateLayout l = vi::LayoutTemplate(o, units, s, pad);
    vi::TemplatePanel(l);
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    std::vector<Debugger::Breakpoint> &list = m_Doc.listBreakpoints;
    if (m_Doc.listPick >= static_cast<int>(list.size())) m_Doc.listPick = -1;
    static const std::vector<vi::ListColumn> COLUMNS{{"BreakpointList", 0.0f}};
    bool twice = false;
    const int row = vi::ListView(
        "##breakpoints", l.Min(5, 5), l.Max(5, 5, 265, 146), s, COLUMNS, static_cast<int>(list.size()),
        [&](int r, int) { return BreakpointLabel(context, list[r].data); }, [&](int r) { return r == m_Doc.listPick; }, &twice,
        nullptr, true);
    if (row >= 0) m_Doc.listPick = row;
    const int pick = m_Doc.listPick;
    if (vi::Button("Remove Breakpoint", l.Min(6, 154), l.Max(6, 154, 81, 10), "Remove Breakpoint", pick >= 0)) {
        list.erase(list.begin() + pick);
        m_Doc.listPick = -1;
    }
    if (vi::Button("Remove All", l.Min(95, 154), l.Max(95, 154, 81, 10), "Remove All", !list.empty())) {
        list.clear();
        m_Doc.listPick = -1;
    }
    bool close = false;
    if (vi::Button("Configure", l.Min(184, 154), l.Max(184, 154, 39, 10), "Configure", pick >= 0)) {
        const Debugger::Breakpoint chosen = list[pick];
        close = true;
        LoadBreakpointConfig(chosen.script, chosen.data.type, chosen.data.id1);
    }
    if ((vi::Button("Goto", l.Min(231, 154), l.Max(231, 154, 39, 10), "Goto", pick >= 0) || (row >= 0 && twice)) &&
        m_Doc.listPick >= 0)
        GoToBreakpoint(list[m_Doc.listPick].script, list[m_Doc.listPick].data);
    const float right = l.right - 60.0f * s;
    const bool cancel = vi::DialogButton("Cancel", ImVec2(right, l.buttonsY), s, ImGuiKey_Escape);
    const bool ok = vi::DialogButton("Ok", ImVec2(right - 70.0f * s, l.buttonsY), s, ImGuiKey_Enter);
    if (ok && m_State.debugger && m_State.HasDocument()) {
        // The ones taken off the list.
        std::vector<Debugger::Breakpoint> removed;
        for (const auto &breakpoint : m_State.debugger->Breakpoints()) {
            bool kept = false;
            for (const auto &other : list)
                kept |= other.script == breakpoint.script && other.data.type == breakpoint.data.type && other.data.id1 == breakpoint.data.id1;
            if (!kept) removed.push_back(breakpoint);
        }
        if (!removed.empty()) RemoveBreakpoints(*m_State.document, removed);
    }
    if (ok || cancel || close) ImGui::CloseCurrentPopup();
    vi::EndDialog();
}

CK_ID SchematicCanvas::ConditionParameterAt(const Hit &hit) const {
    if (!hit.view || !m_State.HasDocument()) return 0;
    if (hit.ref.kind == ElementKind::LocalParam || hit.ref.kind == ElementKind::Shortcut) {
        for (const auto &param : hit.view->model.params)
            if (param.ref == hit.ref) return param.parameter;
        return 0;
    }
    if (hit.ref.kind != ElementKind::Pin) return 0;
    CKObject *owner = m_State.document->Context()->GetObject(hit.ref.object);
    CKObject *pin = nullptr;
    if (CKBehavior *behavior = CKBehavior::Cast(owner)) {
        if (hit.ref.pin == PinKind::ParamOut && hit.ref.index < behavior->GetOutputParameterCount())
            pin = behavior->GetOutputParameter(hit.ref.index);
        if (hit.ref.pin == PinKind::ParamIn && hit.ref.index < behavior->GetInputParameterCount())
            pin = behavior->GetInputParameter(hit.ref.index);
    }
    if (CKParameterOperation *operation = CKParameterOperation::Cast(owner))
        pin = hit.ref.pin == PinKind::ParamOut ? static_cast<CKObject *>(operation->GetOutParameter())
                                               : static_cast<CKObject *>(hit.ref.index == 0 ? operation->GetInParameter1()
                                                                                           : operation->GetInParameter2());
    return pin ? pin->GetID() : 0;
}

void SchematicCanvas::PickConditionParameter(CK_ID parameter) {
    const int row = m_Doc.pickConditionRow;
    m_Doc.pickConditionRow = -1;
    CKObject *object = m_State.document->Context()->GetObject(parameter);
    std::string name = AnsiToUtf8(object && object->GetName() ? object->GetName() : "");
    bool known = false;
    for (const auto &entry : m_Doc.conditionParameters)
        if (entry.first == parameter) {
            known = true;
            name = entry.second;
        }
    if (!known) m_Doc.conditionParameters.emplace_back(parameter, name);
    if (row >= 0 && row < static_cast<int>(m_Doc.conditions.size())) {
        if (m_Doc.pickReference) {
            // Of a type the Schematic Parameter's compares with.
            CKContext *context = m_State.document->Context();
            CKParameterManager *manager = context->GetParameterManager();
            CKObject *compared = context->GetObject(m_Doc.conditions[row].parameter);
            if (manager && compared && object &&
                !manager->IsTypeCompatible(manager->ParameterGuidToType(ParameterType(compared)),
                                           manager->ParameterGuidToType(ParameterType(object)))) {
                m_State.Log(LogLevel::Warning, "Breakpoint: the reference parameter's type is not compatible");
            } else {
                m_Doc.conditions[row].value = name; // a reference parameter is named in Value
            }
        } else {
            m_Doc.conditions[row].parameter = parameter;
            // A comparison of order needs numbers.
            if (m_Doc.conditions[row].comparison >= 4 && !Ordered(m_State.document->Context(), parameter))
                m_Doc.conditions[row].comparison = ByParameter(m_Doc.conditions[row].comparison) ? 2 : 0;
        }
    }
    m_Doc.openBreakpointConfig = true;
}

// Dev "Execute Script(s)" (Alt+X): the selected Scripts, else the current one.
void SchematicCanvas::ExecuteSelectedScripts() {
    if (!m_State.debugger || !m_State.HasDocument()) return;
    std::vector<CK_ID> scripts = m_State.selection.Scripts();
    if (scripts.empty() && m_Doc.currentScript) scripts.push_back(m_Doc.currentScript);
    m_State.debugger->ExecuteScripts(scripts);
    RefreshValues();
    m_State.showDebugger = true;
}

// --------------------------------------------------------------- snapshot

// Dev "Load Snapshot for Script": an image scaled into the header's snapshot place.
void SchematicCanvas::LoadSnapshotFor(CK_ID script) {
    if (!m_State.HasDocument()) return;
    // Read with WIC: the wide path, any name.
    FileDialogOptions options;
    options.filter = L"Images (*.bmp;*.png;*.jpg;*.jpeg;*.gif;*.tif)\0*.bmp;*.png;*.jpg;*.jpeg;*.gif;*.tif;*.tiff\0All files (*.*)\0*.*\0";
    m_State.RequestFile(options, [this, script](const FileDialogResult &selection) {
        if (selection.status != FileDialogResult::Status::Selected) return;
        const std::wstring path = Utf8ToWide(selection.paths.front());
    std::string error;
    // The header's snapshot place is 49 x 38 pixels (library layout).
    void *bitmap = LoadSnapshotBitmap(path, 48, 36, &error);
    if (!bitmap) {
        m_State.Log(LogLevel::Error, "Load Snapshot for Script: " + error);
        return;
    }
    m_State.document->KeepBitmap(bitmap);
    if (!SetScriptSnapshot(*m_State.document, script, bitmap))
        m_State.Log(LogLevel::Warning, "Load Snapshot for Script: the Script's display is read-only");
    });
}

// Dev drag and drop: an image on a Script sets its snapshot; an .nms file on
// a behavior graph replaces it (Replace with File Graph), elsewhere in a
// Script it is imported (Import Behavior Graph).
bool SchematicCanvas::DropFile(const std::wstring &path, const std::string &ansiPath, const ImVec2 &screen) {
    const Hit hit = HitAt(screen);
    if (!hit.view || !m_State.HasDocument()) return false;
    std::wstring extension = path.substr(path.find_last_of(L'.') == std::wstring::npos ? path.size() : path.find_last_of(L'.'));
    for (auto &c : extension) c = static_cast<wchar_t>(towlower(c));
    const CK_ID script = hit.view->id;
    if (extension == L".bmp" || extension == L".png" || extension == L".jpg" || extension == L".jpeg" ||
        extension == L".gif" || extension == L".tif" || extension == L".tiff") {
        std::string error;
        void *bitmap = LoadSnapshotBitmap(path, 48, 36, &error);
        if (!bitmap) {
            m_State.Log(LogLevel::Error, "Snapshot: " + error);
            return true;
        }
        m_State.document->KeepBitmap(bitmap);
        if (!SetScriptSnapshot(*m_State.document, script, bitmap))
            m_State.Log(LogLevel::Warning, "Snapshot: the Script's display is read-only");
        return true;
    }
    if (extension == L".nms") {
        if (hit.ref.kind == ElementKind::Behavior) {
            const BehaviorItem *behavior = hit.view->model.FindBehavior(hit.ref.object);
            if (behavior && behavior->expandable) {
                const CK_ID target = behavior->id;
                auto result = std::make_shared<EditResult>(EditResult::NoChange());
                m_State.RunRuntime("Replacing behavior graph", [this, script, target, ansiPath, result] {
                    *result = ReplaceWithFileGraph(*m_State.document, script, target, ansiPath);
                }, [this, result](bool ok) { if (ok) Report(*result, "Replace with File Graph"); });
                return true;
            }
        }
        const CK_ID graph = GraphAt(*hit.view, hit.local);
        if (!graph) return false;
        const Point local = hit.local;
        auto result = std::make_shared<EditResult>(EditResult::NoChange());
        m_State.RunRuntime("Importing behavior graph", [this, script, graph, ansiPath, local, result] {
            *result = ImportBehaviorGraph(*m_State.document, script, graph, ansiPath, local);
        }, [this, result](bool ok) { if (ok) Report(*result, "Import Behavior Graph"); });
        return true;
    }
    return false;
}

// --------------------------------------------------------------- replace

bool SchematicCanvas::IsBuildingBlockAt(const Hit &hit) const {
    if (!hit.view || hit.ref.kind != ElementKind::Behavior) return false;
    const BehaviorItem *behavior = hit.view->model.FindBehavior(hit.ref.object);
    return behavior && behavior->buildingBlock;
}

void SchematicCanvas::OpenReplaceBlock(CK_ID script, CK_ID behavior, const CKGUID &prototype) {
    m_Doc.replaceScript = script;
    m_Doc.replaceBehavior = behavior;
    m_ReplaceGuid = prototype;
    m_Doc.openReplaceBlock = true;
}

// Dev "Replace Building Block": Yes / Apply to All / No.
void SchematicCanvas::ReplaceBlockDialog() {
    if (m_Doc.openReplaceBlock) {
        ImGui::OpenPopup("Replace Building Block");
        m_Doc.openReplaceBlock = false;
    }
    if (!ImGui::BeginPopupModal("Replace Building Block", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    CKObject *old = context ? context->GetObject(m_Doc.replaceBehavior) : nullptr;
    const BuildingBlockEntry *entry = m_State.catalog.Find(m_ReplaceGuid);
    ImGui::Text("Do you want to replace behavior <%s> with <%s>?",
                old ? AnsiToUtf8(old->GetName() ? old->GetName() : "").c_str() : "",
                entry ? AnsiToUtf8(entry->name).c_str() : "?");
    ImGui::Spacing();
    const bool yes = ImGui::Button("Yes", ImVec2(100, 0));
    ImGui::SameLine();
    const bool all = ImGui::Button("Apply to All", ImVec2(100, 0));
    ImGui::SameLine();
    const bool no = ImGui::Button("No", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (yes || all) {
        std::vector<std::pair<CK_ID, CK_ID>> targets{{m_Doc.replaceScript, m_Doc.replaceBehavior}};
        if (all) targets = SameNamedBehaviors(*m_State.document, m_Doc.replaceBehavior);
        size_t steps = 0;
        for (const EditResult &result : ReplaceBuildingBlocks(*m_State.document, targets, m_ReplaceGuid)) {
            if (result.ok) ++steps;
            else Report(result, "Replace Building Block");
        }
        if (steps) {
            m_State.RememberBlock(m_ReplaceGuid);
            m_State.selection.Clear();
            m_State.Log(LogLevel::Info, "Replaced " + std::to_string(steps) + " Building Block(s)");
        }
    }
    if (yes || all || no) {
        m_Doc.replaceBehavior = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Dev "Replace with" dialog: the behavior graphs of the composition, by Script.
void SchematicCanvas::ReplaceGraphDialog() {
    if (m_Doc.openReplaceGraph) {
        ImGui::OpenPopup("Replace with");
        m_Doc.openReplaceGraph = false;
    }
    ImGui::SetNextWindowSize(ImVec2(400, 380), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Replace with", nullptr)) return;
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    ImGui::TextUnformatted("Behavior List");
    if (context && ImGui::BeginTable("graphs", 2,
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY |
                                         ImGuiTableFlags_Resizable,
                                     ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Behavior");
        ImGui::TableSetupColumn("Script");
        ImGui::TableHeadersRow();
        for (const auto &info : m_State.document->Scripts()) {
            std::vector<CKBehavior *> pending{CKBehavior::Cast(context->GetObject(info.id))};
            while (!pending.empty()) {
                CKBehavior *behavior = pending.back();
                pending.pop_back();
                if (!behavior) continue;
                for (int i = behavior->GetSubBehaviorCount(); i-- > 0;) pending.push_back(behavior->GetSubBehavior(i));
                if (behavior->GetID() == info.id || behavior->GetID() == m_Doc.replaceBehavior ||
                    behavior->GetType() == CKBEHAVIORTYPE_BASE || (behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK))
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(behavior->GetID()));
                if (ImGui::Selectable(AnsiToUtf8(behavior->GetName() ? behavior->GetName() : "").c_str(),
                                      m_Doc.replaceSource == behavior->GetID(), ImGuiSelectableFlags_SpanAllColumns))
                    m_Doc.replaceSource = behavior->GetID();
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(AnsiToUtf8(info.name).c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::Checkbox("Replace All", &m_ReplaceAll);
    const bool ok = ImGui::Button("Ok", ImVec2(100, 0));
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (ok && m_Doc.replaceSource) {
        std::vector<std::pair<CK_ID, CK_ID>> targets{{m_Doc.replaceScript, m_Doc.replaceBehavior}};
        if (m_ReplaceAll) targets = SameNamedBehaviors(*m_State.document, m_Doc.replaceBehavior);
        size_t steps = 0;
        for (const EditResult &result : ReplaceBehaviorGraphs(*m_State.document, targets, m_Doc.replaceSource)) {
            if (result.ok) ++steps;
            else if (!result.Unchanged()) Report(result, "Replace Behavior Graph");
        }
        if (steps) m_State.selection.Clear();
    }
    if ((ok && m_Doc.replaceSource) || cancel) {
        m_Doc.replaceBehavior = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void SchematicCanvas::ReplaceWithFile(CK_ID script, CK_ID graph) {
    m_State.RequestFile(BehaviorFileDialog(false, {}), [this, script, graph](const FileDialogResult &selection) {
        if (selection.status != FileDialogResult::Status::Selected) return;
        const std::string path = selection.paths.front();
        auto result = std::make_shared<EditResult>(EditResult::NoChange());
        m_State.RunRuntime("Replacing behavior graph", [this, script, graph, path, result] {
            *result = ReplaceWithFileGraph(*m_State.document, script, graph, path);
        }, [this, result](bool ok) {
            if (!ok) return;
            Report(*result, "Replace with File Graph");
            if (result->ok) m_State.selection.Clear();
        });
    });
}

// Dev "Confirm Script(s) Deletion" (language pack [Schematic]<Dialog>).
void SchematicCanvas::ConfirmScriptDeletion() {
    if (m_Doc.openDeleteScript) {
        ImGui::OpenPopup("Confirm Script(s) Deletion");
        m_Doc.openDeleteScript = false;
    }
    if (!ImGui::BeginPopupModal("Confirm Script(s) Deletion", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const std::vector<CK_ID> scripts = m_Doc.deleteScripts;
    CKObject *script = m_State.HasDocument() && !scripts.empty() ? m_State.document->Context()->GetObject(scripts.front()) : nullptr;
    if (scripts.size() > 1) ImGui::Text("Delete the %d Scripts selected?", static_cast<int>(scripts.size()));
    else ImGui::Text("Delete the Script <%s>?", script ? AnsiToUtf8(script->GetName() ? script->GetName() : "").c_str() : "");
    if (ImGui::Button("OK", ImVec2(100, 0))) {
        {
            EditGroup group(*m_State.document, scripts.size() > 1 ? "Delete Scripts" : "Delete Script");
            for (CK_ID each : scripts)
                if (m_State.document->Context()->GetObject(each)) Report(DeleteScript(*m_State.document, each), "Delete Script");
        }
        m_State.selection.Clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Dev "Confirm Graph(es) Deletion" ([Schematic]<Dialog>#15, "Warning!"; Yes / No).
void SchematicCanvas::ConfirmGraphDeletion() {
    if (m_Doc.openDeleteGraphs) {
        ImGui::OpenPopup("Warning!###confirmgraphs");
        m_Doc.openDeleteGraphs = false;
    }
    if (!ImGui::BeginPopupModal("Warning!###confirmgraphs", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("Confirm Graph(es) Deletion");
    const bool keys = !ImGui::IsWindowAppearing();
    if (ImGui::Button("Yes", ImVec2(100, 0)) || (keys && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        ImGui::CloseCurrentPopup();
        DeleteSelection(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("No", ImVec2(100, 0)) || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// --------------------------------------------------------------- helpers

const ParamItem *SchematicCanvas::FindParam(const ScriptView &view, const ElementRef &ref) const {
    for (const auto &param : view.model.params)
        if (param.ref == ref) return &param;
    return nullptr;
}

void SchematicCanvas::SetParamStyle(CK_ID script, const ParamItem &param, ParameterStyle style) {
    const CK_ID owner = param.owner;
    const int slot = param.slot;
    const bool shortcut = param.shortcut;
    EditScriptData(*m_State.document, script, "Change Parameter Display", [&](InterfaceData &data) {
        BehaviorData *record = FindRecord(data, owner);
        if (!record) return;
        auto &table = shortcut ? record->sharedParams : record->localParams;
        if (slot >= 0 && slot < static_cast<int>(table.size())) table[slot].style = style;
    });
}

void SchematicCanvas::ToggleCommentIcon(CK_ID script, const ElementRef &ref) {
    ScriptView *view = ViewFor(script);
    if (!view) return;
    for (const auto &comment : view->model.comments) {
        if (comment.ref != ref) continue;
        const CK_ID owner = comment.owner;
        const int slot = comment.slot;
        const bool collapse = !comment.collapsed;
        EditScriptData(*m_State.document, script, collapse ? "Iconify Comment" : "Open Comment",
                       [&](InterfaceData &data) {
                           BehaviorData *record = FindRecord(data, owner);
                           if (record && slot >= 0 && slot < static_cast<int>(record->comments.size()))
                               record->comments[slot].SetCollapsed(collapse);
                       });
        return;
    }
}

// Dev Spacebar: change the display of the selected local parameters,
// iconify/open the selected comments, open/close the selected graphs and
// Scripts.
void SchematicCanvas::SpaceAction() {
    const Selection selection = m_State.selection;
    ScriptView *view = ViewFor(selection.script);
    if (!view) return;
    for (const auto &item : selection.items) {
        switch (item.kind) {
        case ElementKind::LocalParam:
        case ElementKind::Shortcut:
            if (ScriptView *again = ViewFor(selection.script))
                if (const ParamItem *param = FindParam(*again, item)) {
                    const ParamItem copy = *param;
                    SetParamStyle(selection.script, copy, NextStyle(copy.style));
                }
            break;
        case ElementKind::Comment: ToggleCommentIcon(selection.script, item); break;
        case ElementKind::Behavior:
            if (ScriptView *again = ViewFor(selection.script))
                if (const BehaviorItem *behavior = again->model.FindBehavior(item.object))
                    if (behavior->expandable) ToggleFold(selection.script, behavior->id);
            break;
        case ElementKind::Script: ToggleScriptOpen(selection.script); break;
        default: break;
        }
    }
}

// Dev G: the next drag draws the new graph's rectangle.
void SchematicCanvas::BeginDrawGraph() {
    m_DrawGraphMode = true;
    m_State.Log(LogLevel::Info, "Draw Behavior Graph: drag a rectangle around the behaviors to group (Esc cancels)");
}

void SchematicCanvas::EndDrawGraph(ScriptView &view, const Rect &local) {
    m_DrawGraphMode = false;
    std::vector<CK_ID> members;
    for (const auto &ref : ElementsInRect(view.model, local, true))
        if (ref.kind == ElementKind::Behavior) {
            const BehaviorItem *behavior = view.model.FindBehavior(ref.object);
            // Only the outermost behaviors inside the rectangle.
            bool nested = false;
            for (CK_ID other : members) nested = nested || (behavior && behavior->parent == other);
            if (behavior && !nested) members.push_back(behavior->id);
        }
    if (members.empty()) {
        CreateGraphAt(view.id, Point(local.hPos, local.vPos));
        return;
    }
    Report(GroupBehaviors(*m_State.document, view.id, members), "Draw Behavior Graph");
}

void SchematicCanvas::AddThisAt(CK_ID script, const Point &local) {
    ScriptView *view = ViewFor(script);
    const CK_ID graph = view ? GraphAt(*view, local) : 0;
    if (!graph) {
        m_State.Log(LogLevel::Info, "Add <This> parameters in an open Script or an expanded graph");
        return;
    }
    Report(AddThisParameter(*m_State.document, script, graph, local), "Add <This> Parameter");
}

void SchematicCanvas::GoToShortcutSource(CK_ID source) {
    SearchQuery query;
    query.id = source;
    query.kinds = SearchParameters;
    for (const auto &result : SearchDocument(*m_State.document, query)) {
        if (result.ref.kind == ElementKind::Shortcut) continue;
        m_State.events.Post(ShowElement{result.script, result.ref, result.focus});
        return;
    }
    // Not displayed in a Script (an output, an attribute, ...): its owner.
    CKParameter *parameter = CKParameter::Cast(m_State.document->Context()->GetObject(source));
    m_State.Log(LogLevel::Info, "The shortcut source is not displayed in a Script: " +
                                    ObjectText(parameter ? parameter->GetOwner() : nullptr));
}

void SchematicCanvas::OpenHtmlHelp(CK_ID behaviorId) {
    CKBehavior *behavior = CKBehavior::Cast(m_State.document->Context()->GetObject(behaviorId));
    const BuildingBlockEntry *entry =
        behavior && (behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK) ? m_State.catalog.Find(behavior->GetPrototypeGuid())
                                                                     : nullptr;
    if (!entry) {
        m_State.Log(LogLevel::Info, "Html Help is available for Building Blocks");
        return;
    }
    OpenHtmlHelpOf(*entry);
}

void SchematicCanvas::OpenHtmlHelpOf(const BuildingBlockEntry &bb) {
    if (!HelpAvailable()) return;
    const BuildingBlockEntry *entry = &bb;
    // documentation.chm (resources/data): behaviors/<top category>/<name>.html.
    const std::string category = entry->category.substr(0, entry->category.find('/'));
    const std::string topic = "behaviors/" + AnsiToUtf8(category) + "/" + AnsiToUtf8(entry->name) + ".html";
    m_State.events.Post(ShowHelpTopic{topic});
}

// Dev's Script header sorts (documentation): the order of the stack.
void SchematicCanvas::SortScripts(ScriptSort sort) {
    if (!m_State.HasDocument()) return;
    m_State.scriptOrder = vdev::SortScripts(*m_State.document, m_State.visibleScripts, sort);
    m_State.events.Post(RefreshScriptStack{});
}

// ------------------------------------------------------------- dialogs

void SchematicCanvas::CkPropertiesDialog() {
    if (m_Doc.openCkProperties) {
        ImGui::OpenPopup("CK Properties");
        m_Doc.openCkProperties = false;
    }
    if (!ImGui::BeginPopupModal("CK Properties", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    CKObject *object = context ? context->GetObject(m_Doc.ckPropertiesObject) : nullptr;
    if (!object) {
        ImGui::TextUnformatted("The object no longer exists");
    } else {
        ImGui::Text("Name: %s", AnsiToUtf8(object->GetName() ? object->GetName() : "").c_str());
        ImGui::Text("CK_ID: %u", static_cast<unsigned>(object->GetID()));
        ImGui::Text("Class: %s", CKClassIDToString(object->GetClassID()));
        if (CKParameter *parameter = CKParameter::Cast(object)) {
            CKParameterManager *manager = context->GetParameterManager();
            const char *type = manager ? manager->ParameterTypeToName(parameter->GetType()) : nullptr;
            ImGui::Text("Type: %s", type ? AnsiToUtf8(type).c_str() : "?"); // a user type's name is ANSI
            ImGui::Text("Owner: %s", ObjectText(parameter->GetOwner()).c_str());
            if (CKParameterLocal *local = CKParameterLocal::Cast(parameter))
                if (local->IsMyselfParameter()) ImGui::TextUnformatted("<This> parameter (the Script owner)");
            if (CKParameterOut *out = CKParameterOut::Cast(parameter)) {
                ImGui::Text("Destinations: %d", out->GetDestinationCount());
                for (int i = 0; i < out->GetDestinationCount(); ++i)
                    ImGui::BulletText("%s", ObjectText(out->GetDestination(i)).c_str());
            }
        } else if (CKParameterIn *input = CKParameterIn::Cast(object)) {
            ImGui::Text("Owner: %s", ObjectText(input->GetOwner()).c_str());
            ImGui::Text("Direct source: %s", ObjectText(input->GetDirectSource()).c_str());
            ImGui::Text("Shared source: %s", ObjectText(input->GetSharedSource()).c_str());
            ImGui::Text("Enabled: %s", input->IsEnabled() ? "yes" : "no");
        }
    }
    if (ImGui::Button("Close", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Dev "Analyze Shortcut Source": the shortcuts of a source; Goto focuses one.
void SchematicCanvas::ShortcutAnalysisDialog() {
    if (m_Doc.openShortcutAnalysis) {
        ImGui::OpenPopup("Shortcut Source Analysis");
        m_Doc.openShortcutAnalysis = false;
        SearchQuery query;
        query.id = m_Doc.shortcutSource;
        query.kinds = SearchParameters;
        m_Doc.shortcutResults.clear();
        for (auto &result : SearchDocument(*m_State.document, query))
            if (result.ref.kind == ElementKind::Shortcut) m_Doc.shortcutResults.push_back(result);
        m_Doc.shortcutPick = -1;
    }
    if (!ImGui::BeginPopupModal("Shortcut Source Analysis", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("Shortcuts of %s", ObjectText(m_State.document->Context()->GetObject(m_Doc.shortcutSource)).c_str());
    bool go = false;
    if (ImGui::IsWindowAppearing()) ImGui::SetNextWindowFocus();
    if (ImGui::BeginListBox("##shortcuts", ImVec2(520, 220))) {
        TypeAhead typeAhead;
        for (int i = 0; i < static_cast<int>(m_Doc.shortcutResults.size()); ++i) {
            const SearchResult &result = m_Doc.shortcutResults[i];
            const ScriptInfo *info = m_State.document->FindScript(result.script);
            const std::string label = (info ? AnsiToUtf8(info->name) : std::string("?")) + "  /  " + result.label;
            if (typeAhead.Selectable((label + "##" + std::to_string(i)).c_str(), m_Doc.shortcutPick == i,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                m_Doc.shortcutPick = i;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) go = true;
            }
        }
        ImGui::EndListBox();
    }
    if (m_Doc.shortcutResults.empty()) ImGui::TextDisabled("No shortcut displays this parameter");
    ImGui::BeginDisabled(m_Doc.shortcutPick < 0);
    if (ImGui::Button("Goto", ImVec2(100, 0))) go = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    if (go && m_Doc.shortcutPick >= 0) {
        const SearchResult &result = m_Doc.shortcutResults[m_Doc.shortcutPick];
        m_State.events.Post(ShowElement{result.script, result.ref, result.focus});
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Dev "Add Building Block by Name" (Ctrl+double-click): a filtered list;
// Return takes the highlighted (or only) entry, Escape cancels.
void SchematicCanvas::QuickAddDialog() {
    if (m_Doc.openQuickAdd) {
        ImGui::OpenPopup("Add Building Block by Name");
        m_Doc.openQuickAdd = false;
        m_QuickAddText.clear();
        m_QuickAddPick = 0;
    }
    if (!ImGui::BeginPopup("Add Building Block by Name")) return;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(320);
    if (ImGui::InputText("##name", &m_QuickAddText)) m_QuickAddPick = 0;
    std::vector<const BuildingBlockEntry *> matches;
    for (const auto &entry : m_State.catalog.Entries()) {
        const std::string name = AnsiToUtf8(entry.name);
        if (m_QuickAddText.empty() || _strnicmp(name.c_str(), m_QuickAddText.c_str(), m_QuickAddText.size()) == 0)
            matches.push_back(&entry);
    }
    std::sort(matches.begin(), matches.end(), [](const BuildingBlockEntry *a, const BuildingBlockEntry *b) {
        return _stricmp(a->name.c_str(), b->name.c_str()) < 0;
    });
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) m_QuickAddPick = std::min<int>(m_QuickAddPick + 1, int(matches.size()) - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_QuickAddPick = std::max(m_QuickAddPick - 1, 0);
    const BuildingBlockEntry *chosen = nullptr;
    if (ImGui::BeginListBox("##matches", ImVec2(320, 240))) {
        for (int i = 0; i < static_cast<int>(matches.size()) && i < 500; ++i) {
            const bool picked = i == m_QuickAddPick;
            if (ImGui::Selectable((AnsiToUtf8(matches[i]->name) + "##" + std::to_string(i)).c_str(), picked))
                chosen = matches[i];
            if (picked && ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) ImGui::SetScrollHereY();
        }
        ImGui::EndListBox();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && m_QuickAddPick >= 0 && m_QuickAddPick < static_cast<int>(matches.size()))
        chosen = matches[m_QuickAddPick];
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    if (chosen) {
        InsertBlockAt(chosen->guid, m_Doc.quickAddScript, m_Doc.quickAddLocal);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void SchematicCanvas::OpenQuickAdd(CK_ID script, const Point &local) {
    m_Doc.quickAddScript = script;
    m_Doc.quickAddLocal = local;
    m_Doc.openQuickAdd = true;
}

// Dev "Set Color": the stored header color (COLORREF) of the Script.
void SchematicCanvas::ScriptColorDialog() {
    if (m_Doc.openScriptColor) {
        ImGui::OpenPopup("Script Color");
        m_Doc.openScriptColor = false;
        ScriptSession *session = m_Doc.colorScripts.empty() ? nullptr : m_State.document->Session(m_Doc.colorScripts.front());
        const CKDWORD color = session ? session->Data().header.color : InterfaceChunkFormat::DEFAULT_HEADER_COLOR;
        m_ScriptColor[0] = (color & 0xFF) / 255.0f;
        m_ScriptColor[1] = ((color >> 8) & 0xFF) / 255.0f;
        m_ScriptColor[2] = ((color >> 16) & 0xFF) / 255.0f;
    }
    if (!ImGui::BeginPopupModal("Script Color", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::ColorPicker3("##color", m_ScriptColor, ImGuiColorEditFlags_NoSidePreview);
    if (ImGui::Button("OK", ImVec2(100, 0))) {
        const CKDWORD color = static_cast<CKDWORD>(m_ScriptColor[0] * 255.0f + 0.5f) |
                              static_cast<CKDWORD>(m_ScriptColor[1] * 255.0f + 0.5f) << 8 |
                              static_cast<CKDWORD>(m_ScriptColor[2] * 255.0f + 0.5f) << 16;
        EditGroup group(*m_State.document, "Set Color");
        for (CK_ID each : m_Doc.colorScripts)
            EditScriptData(*m_State.document, each, "Set Color", [&](InterfaceData &data) { data.header.color = color; });
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// --------------------------------------------------------------- menus

void SchematicCanvas::CommonEditItems(bool rename, bool saveAs) {
    if (ImGui::MenuItem("Draw Behavior Graph", "G")) BeginDrawGraph();
    ImGui::Separator();
    if (rename && ImGui::MenuItem("Rename", "F2")) BeginRename();
    if (saveAs && ImGui::MenuItem("Save As...", "Alt+S")) SaveSelectionAsNms();
    if (ImGui::MenuItem("Cut", "Ctrl+X")) Cut();
    if (ImGui::MenuItem("Copy", "Ctrl+C")) Copy();
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Delete")) DeleteSelection();
}

void SchematicCanvas::BodyMenu(ScriptView &view, const Point &local) {
    const CK_ID script = view.id;
    if (ImGui::MenuItem("Draw Behavior Graph", "G")) BeginDrawGraph();
    ImGui::Separator();
    if (ImGui::BeginMenu("Add Building Block")) {
        // The last ones used first (documentation "Script": "the last used
        // BBs are also available"), then the categories.
        CKGUID chosen;
        bool picked = false;
        for (const CKGUID &guid : m_State.recentBlocks)
            if (const BuildingBlockEntry *entry = m_State.catalog.Find(guid))
                if (ImGui::MenuItem(AnsiToUtf8(entry->name).c_str())) {
                    chosen = guid;
                    picked = true;
                }
        if (!m_State.recentBlocks.empty()) ImGui::Separator();
        if (BlockCategoryMenu(chosen) || picked) InsertBlockAt(chosen, script, local);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Add Building Block by Name", "Ctrl+Left Dbl Click")) OpenQuickAdd(script, local);
    if (ImGui::MenuItem("Add Local Parameter", "Alt+L")) OpenLocalParameterPicker(script, local);
    if (ImGui::MenuItem("Add <This> Parameter", "Alt+T")) AddThisAt(script, local);
    if (ImGui::MenuItem("Add Parameter Operation", "Alt+P")) OpenOperationPicker(script, local);
    if (ImGui::MenuItem("Add Comment", "C")) AddComment(script, local);
    if (!m_State.clipboard.Empty()) {
        ImGui::Separator();
        if (ImGui::MenuItem("Paste", "Ctrl+V")) {
            m_Doc.mouseScript = script;
            m_Doc.mouseLocal = local;
            Paste();
        }
        if (!m_State.clipboard.parameters.empty() && ImGui::MenuItem("Paste as Shortcut", "Shift+Ctrl+V")) {
            m_Doc.mouseScript = script;
            m_Doc.mouseLocal = local;
            PasteShortcuts();
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2")) {
        m_State.selection.Set(script, view.model.script.ref);
        BeginRename();
    }
    if (ImGui::MenuItem("Save As...", "Alt+S")) SaveBehaviorsAs({script}, view.model.script.name + ".nms");
    if (ImGui::MenuItem("Copy", "Ctrl+C")) {
        m_State.objectClipboard.SetScripts(*m_State.document, {script});
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Delete")) {
        m_Doc.deleteScripts = {script};
        m_Doc.openDeleteScript = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Import Behavior Graph")) ImportBehaviorFile(script, local);
}

void SchematicCanvas::ScriptHeaderMenu(ScriptView &view) {
    const CK_ID script = view.id;
    // On a header among those selected: they all; else this one.
    if (!m_State.selection.Contains(script, view.model.script.ref)) m_State.selection.Set(script, view.model.script.ref);
    const std::vector<CK_ID> scripts = m_State.selection.Scripts();
    if (ImGui::MenuItem("Execute Script(s)", "Alt+X")) ExecuteSelectedScripts();
    if (ImGui::MenuItem("Set Initial Conditions", "Shift+Q")) InitialConditionsOfScripts(0);
    if (ImGui::MenuItem("Remove Initial Conditions", "Ctrl+Q")) InitialConditionsOfScripts(1);
    if (ImGui::MenuItem("Restore Initial Conditions", "Alt+Q")) InitialConditionsOfScripts(2);
    ImGui::Separator();
    CKBehavior *behavior = CKBehavior::Cast(m_State.document->Context()->GetObject(script));
    if (ImGui::MenuItem("Show Object In Level Manager", "(double-click on object name)") && behavior &&
        behavior->GetOwner())
        m_State.events.Post(ShowInLevelManager{behavior->GetOwner()->GetID()});
    if (ImGui::MenuItem("Show Script In Level Manager")) m_State.events.Post(ShowInLevelManager{script});
    if (ImGui::MenuItem("Setup") && behavior && behavior->GetOwner())
        m_State.events.Post(ShowObjectSetup{behavior->GetOwner()->GetID()});
    ImGui::Separator();
    if (ImGui::MenuItem("Load Snapshot for Script")) LoadSnapshotFor(script);
    if (view.model.script.snapshot && ImGui::MenuItem("Remove Script's Snapshot"))
        SetScriptSnapshot(*m_State.document, script, nullptr);
    if (ImGui::MenuItem("Edit Priority", "Y")) OpenPriorityDialog();
    ImGui::Separator();
    if (ImGui::MenuItem("Sort By Name")) SortScripts(ScriptSort::Name);
    if (ImGui::MenuItem("Sort By Owner")) SortScripts(ScriptSort::Owner);
    if (ImGui::MenuItem("Sort By Color")) SortScripts(ScriptSort::Color);
    if (ImGui::MenuItem("Sort By Class ID")) SortScripts(ScriptSort::ClassId);
    if (ImGui::MenuItem("Invert Order")) SortScripts(ScriptSort::Invert);
    ImGui::Separator();
    if (ImGui::MenuItem("Set Color")) {
        m_Doc.colorScripts = scripts;
        m_Doc.openScriptColor = true;
    }
    if (ImGui::MenuItem("Remove Color")) {
        EditGroup group(*m_State.document, "Remove Color");
        for (CK_ID each : scripts)
            EditScriptData(*m_State.document, each, "Remove Color",
                           [](InterfaceData &data) { data.header.color = InterfaceChunkFormat::DEFAULT_HEADER_COLOR; });
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2", false, scripts.size() == 1)) BeginRename();
    if (ImGui::MenuItem("Save As...", "Alt+S")) SaveBehaviorsAs(scripts, view.model.script.name + ".nms");
    if (ImGui::MenuItem("Copy")) {
        m_State.objectClipboard.SetScripts(*m_State.document, scripts);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete")) {
        m_Doc.deleteScripts = scripts;
        m_Doc.openDeleteScript = true;
    }
    // The editor's own: a Script whose layout could not be made from the
    // runtime graph when it was opened (a cleared or unreadable chunk is
    // rebuilt then) stays read-only; this tries again (as loading, not an
    // undo step).
    ScriptSession *session = m_State.document->Session(script);
    if (session && session->Origin() == SessionOrigin::LoadFailed) {
        ImGui::Separator();
        if (ImGui::MenuItem("Rebuild Layout from Runtime")) {
            session->Reconstruct(m_State.document->Context());
            Invalidate();
        }
        if (ImGui::IsItemHovered() && !session->LoadMessage().empty())
            ImGui::SetTooltip("%s", session->LoadMessage().c_str());
    }
}

void SchematicCanvas::BehaviorMenu(ScriptView &view, const BehaviorItem &behavior) {
    CKBehavior *runtime = CKBehavior::Cast(m_State.document->Context()->GetObject(behavior.id));
    if (ImGui::MenuItem("Edit Parameters", "E")) OpenParametersDialog();
    if (behavior.settings && ImGui::MenuItem("Edit Settings", "S")) OpenSettingsDialog();
    if (ImGui::BeginMenu("Construct")) {
        const CKDWORD flags = runtime ? runtime->GetFlags() : 0;
        // Graphs take any pin; a Building Block only those its prototype
        // declares variable (CKBEHAVIOR_VARIABLE*).
        if (behavior.expandable) {
            if (ImGui::MenuItem("Add Behavior Input", "I")) AddGraphIo(true);
            if (ImGui::MenuItem("Add Behavior Output", "O")) AddGraphIo(false);
            if (ImGui::MenuItem("Add Parameter Input", "Alt+I")) OpenGraphParameterPicker(true);
            if (ImGui::MenuItem("Add Parameter Output", "Alt+O")) OpenGraphParameterPicker(false);
        } else {
            // Through the Building Block's callback (CKM_BEHAVIOREDITED).
            bool any = false;
            if (flags & CKBEHAVIOR_VARIABLEINPUTS) {
                any = true;
                if (ImGui::MenuItem("Add Behavior Input", "I")) AddGraphIo(true);
            }
            if (flags & CKBEHAVIOR_VARIABLEOUTPUTS) {
                any = true;
                if (ImGui::MenuItem("Add Behavior Output", "O")) AddGraphIo(false);
            }
            if (flags & CKBEHAVIOR_VARIABLEPARAMETERINPUTS) {
                any = true;
                if (ImGui::MenuItem("Add Parameter Input", "Alt+I")) OpenGraphParameterPicker(true);
            }
            if (flags & CKBEHAVIOR_VARIABLEPARAMETEROUTPUTS) {
                any = true;
                if (ImGui::MenuItem("Add Parameter Output", "Alt+O")) OpenGraphParameterPicker(false);
            }
            if (!any) ImGui::TextDisabled("This Building Block has a fixed set of pins");
        }
        ImGui::EndMenu();
    }
    if (runtime && runtime->IsTargetable() &&
        ImGui::MenuItem(runtime->IsUsingTarget() ? "Remove Target Parameter" : "Add Target Parameter", "T"))
        ToggleTargetOfSelection();
    if (ImGui::MenuItem("Edit Priority", "Y")) OpenPriorityDialog();
    if (behavior.buildingBlock && ImGui::MenuItem("Html Help", "F1", false, HelpAvailable())) OpenHtmlHelp(behavior.id);
    BreakpointItems();
    if (behavior.expandable) {
        ImGui::Separator();
        if (ImGui::MenuItem(behavior.folded ? "Open Graph" : "Close Graph", "Spacebar")) ToggleFold(view.id, behavior.id);
        if (ImGui::MenuItem("Delete but Keep Contents", "Shift+Del")) UngroupSelection();
        if (ImGui::BeginMenu("Replace with")) {
            if (ImGui::MenuItem("Replace with existing Graph")) {
                m_Doc.replaceScript = view.id;
                m_Doc.replaceBehavior = behavior.id;
                m_Doc.replaceSource = 0;
                m_ReplaceAll = false;
                m_Doc.openReplaceGraph = true;
            }
            if (ImGui::MenuItem("Replace with File Graph")) ReplaceWithFile(view.id, behavior.id);
            ImGui::EndMenu();
        }
    }
    ImGui::Separator();
    CommonEditItems(behavior.expandable, true);
}

void SchematicCanvas::ParameterMenu(ScriptView &view, const ParamItem &param) {
    const CK_ID script = view.id;
    CKParameter *parameter = CKParameter::Cast(m_State.document->Context()->GetObject(param.parameter));
    if (ImGui::MenuItem("Edit Parameter", "E"))
        OpenValueDialog(script, parameter);
    if (param.shortcut) {
        if (ImGui::MenuItem("Go to Shortcut Source")) GoToShortcutSource(param.parameter);
        if (ImGui::MenuItem("Set Shortcut Source")) BeginPickShortcutSource(param.parameter);
    } else if (param.shortcutSource && ImGui::MenuItem("Analyze Shortcut Source")) {
        m_Doc.shortcutSource = param.parameter;
        m_Doc.openShortcutAnalysis = true;
    }
    if (ImGui::BeginMenu("Change Parameter Display", "Spacebar")) {
        const ParamItem copy = param;
        for (ParameterStyle style : {PARAM_STYLE_COLLAPSED, PARAM_STYLE_VALUE, PARAM_STYLE_NAME, PARAM_STYLE_NAMEVALUE})
            if (style != ParamDisplayMode(param.style) && ImGui::MenuItem(StyleName(style))) SetParamStyle(script, copy, style);
        ImGui::EndMenu();
    }
    if (!param.shortcut &&
        ImGui::MenuItem(m_Doc.menuScripts.empty() ? "Convert to Shortcut Source" : "Convert to Shortcut Source (Selected Scripts)"))
        ConvertToShortcutSource(param.parameter, m_Doc.menuScripts);
    if (ImGui::MenuItem("CK Properties", "K")) {
        m_Doc.ckPropertiesObject = param.parameter;
        m_Doc.openCkProperties = true;
    }
    BreakpointItems();
    ImGui::Separator();
    CommonEditItems(!param.shortcut, false);
}

void SchematicCanvas::LinkMenu(ScriptView &view, const LinkItem &link, const Point &local) {
    if (link.behaviorLink && ImGui::MenuItem("Edit Link Delay", "E")) {
        m_Doc.dialogScript = view.id;
        m_Doc.delayLink = link.ref.object;
        m_DelayValue = link.delay;
        m_Doc.openDelay = true;
    }
    if (!link.conversion.empty() && ImGui::MenuItem("Edit Parameter Operation Link", "E"))
        OpenOperationChoice(OperationChoice::ReplaceConversion, view.id, link.start, link.end, link.ref);
    if (link.path.size() >= 2 && ImGui::MenuItem("Insert Control Point")) {
        // The nearest segment i lies between path[i] and path[i+1]; stored
        // points are path[1..n-2], so insert at index i.
        size_t segment = 0;
        float best = FLT_MAX;
        for (size_t i = 1; i < link.path.size(); ++i) {
            const float distance = DistanceToPath({link.path[i - 1], link.path[i]}, local);
            if (distance < best) {
                best = distance;
                segment = i - 1;
            }
        }
        const Point point(SchematicGrid::SnapToGrid(local.h, SchematicGrid::SPACING),
                          SchematicGrid::SnapToGrid(local.v, SchematicGrid::SPACING));
        const CK_ID owner = link.owner;
        const int slot = link.slot;
        EditScriptData(*m_State.document, view.id, "Insert Control Point", [&](InterfaceData &data) {
            BehaviorData *record = FindRecord(data, owner);
            if (record && slot < static_cast<int>(record->links.size()))
                record->links[slot].InsertControlPoint(static_cast<int>(segment), point);
        });
    }
    if (!link.behaviorLink && link.conversion.empty() && ImGui::MenuItem("Add Parameter Operation in Link"))
        OpenOperationChoice(OperationChoice::InsertInLink, view.id, link.start, link.end, link.ref);
    BreakpointItems();
    ImGui::Separator();
    if (ImGui::MenuItem("Draw Behavior Graph", "G")) BeginDrawGraph();
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Delete")) DeleteSelection();
}

void SchematicCanvas::ContextMenu() {
    PushDevMenuStyle();
    if (ImGui::BeginPopup("SchematicContext")) {
        ContextMenuItems();
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
}

void SchematicCanvas::ContextMenuItems() {
    ScriptView *view = m_Doc.menuHit.view && m_State.HasDocument() ? ViewFor(m_Doc.menuHit.view->id) : nullptr;
    if (!view) {
        if (ImGui::MenuItem("Reset Schematic Zoom and Position", "R")) ResetView();
        return;
    }
    const Hit hit = m_Doc.menuHit;
    const SchematicModel &model = view->model;
    switch (hit.ref.kind) {
    case ElementKind::Script:
        ScriptHeaderMenu(*view);
        break;
    case ElementKind::Behavior:
        if (const BehaviorItem *behavior = model.FindBehavior(hit.ref.object)) {
            const BehaviorItem copy = *behavior;
            BehaviorMenu(*view, copy);
        }
        break;
    case ElementKind::LocalParam:
    case ElementKind::Shortcut:
        if (const ParamItem *param = FindParam(*view, hit.ref)) {
            const ParamItem copy = *param;
            ParameterMenu(*view, copy);
        }
        break;
    case ElementKind::Comment: {
        bool collapsed = false;
        for (const auto &comment : model.comments)
            if (comment.ref == hit.ref) collapsed = comment.collapsed;
        if (ImGui::MenuItem(collapsed ? "Open Comment" : "Iconify Comment", "Spacebar"))
            ToggleCommentIcon(view->id, hit.ref);
        ImGui::Separator();
        if (ImGui::MenuItem("Draw Behavior Graph", "G")) BeginDrawGraph();
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2"))
            for (const auto &comment : model.comments)
                if (comment.ref == hit.ref) {
                    m_Doc.commentScript = view->id;
                    m_Doc.commentRef = hit.ref;
                    m_CommentText = comment.text;
                    m_Doc.openCommentEditor = true;
                }
        if (ImGui::MenuItem("Cut", "Ctrl+X")) Cut();
        if (ImGui::MenuItem("Copy", "Ctrl+C")) Copy();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Delete")) DeleteSelection();
        break;
    }
    case ElementKind::Link:
        for (const auto &link : model.links)
            if (link.ref == hit.ref) {
                const LinkItem copy = link;
                LinkMenu(*view, copy, hit.local);
                break;
            }
        break;
    case ElementKind::ControlPoint:
        if (ImGui::MenuItem("Remove Control Point", "Delete")) DeleteSelection();
        break;
    case ElementKind::Operation:
        if (ImGui::MenuItem("Edit Parameter Operation", "M")) OpenModifyOperation();
        if (ImGui::MenuItem("Edit Parameters", "E")) OpenParametersDialog();
        BreakpointItems();
        ImGui::Separator();
        CommonEditItems(true, false);
        break;
    case ElementKind::Pin: {
        CKObject *owner = m_State.document->Context()->GetObject(hit.ref.object);
        CKBehavior *behavior = CKBehavior::Cast(owner);
        CKParameterOperation *operation = CKParameterOperation::Cast(owner);
        CKObject *pin = nullptr;
        if (behavior) {
            switch (hit.ref.pin) {
            case PinKind::ParamIn: pin = behavior->GetInputParameter(hit.ref.index); break;
            case PinKind::ParamOut: pin = behavior->GetOutputParameter(hit.ref.index); break;
            case PinKind::Target: pin = behavior->GetTargetParameter(); break;
            default: break;
            }
        } else if (operation) {
            // A parameter operation's inputs and output.
            if (hit.ref.pin == PinKind::ParamIn)
                pin = hit.ref.index == 0 ? static_cast<CKObject *>(operation->GetInParameter1())
                                         : hit.ref.index == 1 ? operation->GetInParameter2() : nullptr;
            else if (hit.ref.pin == PinKind::ParamOut)
                pin = operation->GetOutParameter();
        }
        if (pin && ImGui::MenuItem("Edit Parameter", "E")) OpenValueDialog(view->id, pin);
        if (pin && ImGui::MenuItem("CK Properties", "K")) {
            m_Doc.ckPropertiesObject = pin->GetID();
            m_Doc.openCkProperties = true;
        }
        // Documentation "Parameter Shortcut": a parameter output copied with
        // the menu (pasted as a local, or as a shortcut with Ctrl+Shift+V).
        if (pin && hit.ref.pin == PinKind::ParamOut && ImGui::MenuItem("Copy", "Ctrl+C")) Copy();
        BreakpointItems();
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2")) BeginRename();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Delete")) DeleteSelection();
        break;
    }
    default:
        BodyMenu(*view, hit.local);
        break;
    }
}

} // namespace vdev
