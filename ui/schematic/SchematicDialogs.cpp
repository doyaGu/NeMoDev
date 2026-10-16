// Modal dialogs and pickers of the Schematic canvas.
#include "ui/schematic/SchematicCanvas.h"

#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/TypeAhead.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include "core/graph/RuntimeGraph.h"
#include "core/schematic/SchematicEdits.h"
#include "core/graph/ValueCommands.h"
#include "core/runtime/TextEncoding.h"

namespace vdev {

void SchematicCanvas::CommentEditor() {
    if (m_Doc.openCommentEditor) {
        ImGui::OpenPopup("Edit Comment");
        m_Doc.openCommentEditor = false;
    }
    if (!ImGui::BeginPopupModal("Edit Comment", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextMultiline("##text", &m_CommentText, ImVec2(420, 160));
    if (ImGui::Button("OK", ImVec2(100, 0))) {
        const std::string text = Utf8ToAnsi(m_CommentText);
        const ElementRef ref = m_Doc.commentRef;
        EditScriptData(*m_State.document, m_Doc.commentScript, "Edit Comment", [&](InterfaceData &data) {
            BehaviorData *record = FindRecord(data, ref.owner);
            if (record && ref.index >= 0 && ref.index < static_cast<int>(record->comments.size()))
                record->comments[ref.index].text = text;
        });
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SchematicCanvas::RenameDialog() {
    if (m_Doc.openRename) {
        ImGui::OpenPopup("Rename");
        m_Doc.openRename = false;
    }
    if (!ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool entered = ImGui::InputText("Name", &m_RenameText, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("OK", ImVec2(100, 0)) || entered) {
        Report(RenameObject(*m_State.document, m_Doc.dialogScript, m_Doc.renameObject, Utf8ToAnsi(m_RenameText)),
               "Rename");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void SchematicCanvas::DelayDialog() {
    if (m_Doc.openDelay) {
        ImGui::OpenPopup("Link Delay");
        m_Doc.openDelay = false;
    }
    if (!ImGui::BeginPopupModal("Link Delay", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputInt("Frames", &m_DelayValue);
    m_DelayValue = std::max(0, std::min(32764, m_DelayValue));
    if (ImGui::Button("OK", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        Report(SetLinkDelay(*m_State.document, m_Doc.dialogScript, m_Doc.delayLink, m_DelayValue), "Link delay");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

namespace {

bool MatchesFilter(const std::string &text, const std::string &filter) {
    if (filter.empty()) return true;
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    return lower(text).find(lower(filter)) != std::string::npos;
}

} // namespace

void SchematicCanvas::LocalParameterDialog() {
    if (m_Doc.openLocalPicker) {
        ImGui::OpenPopup("Add Parameter");
        m_Doc.openLocalPicker = false;
    }
    if (!ImGui::BeginPopupModal("Add Parameter", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextDisabled("%s", m_TypePurpose == TypePurpose::Local        ? "Local parameter"
                              : m_TypePurpose == TypePurpose::GraphInput ? "Input parameter"
                                                                         : "Output parameter");
    ImGui::InputText("Name", &m_LocalName);
    ImGui::InputTextWithHint("##filter", "Filter types", &m_PickerFilter);
    if (ImGui::BeginListBox("##types", ImVec2(360, 260))) {
        TypeAhead typeAhead;
        for (int i = 0; i < static_cast<int>(m_Types.size()); ++i) {
            const std::string name = AnsiToUtf8(m_Types[i].name);
            if (!MatchesFilter(name, m_PickerFilter)) continue;
            if (typeAhead.Selectable(name.c_str(), i == m_TypeIndex)) m_TypeIndex = i;
        }
        ImGui::EndListBox();
    }
    const bool ready = m_TypeIndex >= 0 && m_TypeIndex < static_cast<int>(m_Types.size()) && !m_LocalName.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Add", ImVec2(100, 0))) {
        if (m_TypePurpose == TypePurpose::Local) {
            Report(AddLocalParameter(*m_State.document, m_Doc.dialogScript, m_Doc.dialogGraph,
                                     Utf8ToAnsi(m_LocalName), m_Types[m_TypeIndex].guid, m_Doc.dialogPosition),
                   "Add local parameter");
            m_State.view.showLocalParams = true;
        } else {
            const bool input = m_TypePurpose == TypePurpose::GraphInput;
            const PinKind kind = input ? PinKind::ParamIn : PinKind::ParamOut;
            CKBehavior *owner = CKBehavior::Cast(m_State.document->Context()->GetObject(m_Doc.pinOwner));
            if (AcceptsVariablePin(owner, kind))
                Report(AddBuildingBlockPin(*m_State.document, m_Doc.dialogScript, m_Doc.pinOwner, kind,
                                           Utf8ToAnsi(m_LocalName), m_Types[m_TypeIndex].guid),
                       input ? "Add input parameter" : "Add output parameter");
            else
                Report(AddGraphPin(*m_State.document, m_Doc.dialogScript, m_Doc.pinOwner, kind,
                                   Utf8ToAnsi(m_LocalName), m_Types[m_TypeIndex].guid),
                       input ? "Add input parameter" : "Add output parameter");
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Dev's Add Parameter Operation dialog (Dev.exe sub_595220): Inputs (two
// types), Operation and Output, each with its lock in the Easy Finding mode
// (Edit Parameter Operation Mode 1; a locked part narrows the others' lists
// to what fits it, sub_5969F0); in the Quick Finding mode (0, 1.0.1 style)
// the Operation list holds what the three types allow (sub_595CC0). The
// line below tells whether the four make an operation (sub_5977D0: language
// strings 634 / 635). The parts stay from one opening to the next, as Dev's.
void SchematicCanvas::RefreshOperationChoices() {
    CKContext *context = m_State.document->Context();
    if (m_Types.empty()) m_Types = ListParameterTypes(context);
    if (m_Operations.empty()) m_Operations = ListParameterOperations(context);
    const bool easy = m_State.preferences.Int("Schematic\\Edit Parameter Operation Mode") != 0;
    if (!easy)
        for (bool &lock : m_OperationLocks) lock = false;
    std::vector<CKGUID> types{CKPGUID_NONE};
    for (const ParameterTypeEntry &type : m_Types) types.push_back(type.guid);
    std::vector<CKGUID> operations;
    for (const OperationEntry &operation : m_Operations) operations.push_back(operation.guid);
    auto distinct = [](const std::vector<OperationSignature> &signatures, int part) {
        std::vector<CKGUID> values;
        for (const OperationSignature &signature : signatures) {
            const CKGUID value = part == 0 ? signature.input1 : part == 1 ? signature.input2
                                 : part == 2 ? signature.result : signature.operation;
            if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
        }
        return values;
    };
    const CKGUID *parts = m_OperationParts;
    const bool anyLock = std::any_of(std::begin(m_OperationLocks), std::end(m_OperationLocks), [](bool l) { return l; });
    for (int part = 0; part < 4; ++part) m_OperationChoices[part] = part < 3 ? types : operations;
    if (anyLock) {
        const std::vector<OperationSignature> fitting = AvailableOperationSignatures(
            context, m_OperationLocks[3] ? &parts[3] : nullptr, m_OperationLocks[2] ? &parts[2] : nullptr,
            m_OperationLocks[0] ? &parts[0] : nullptr, m_OperationLocks[1] ? &parts[1] : nullptr);
        for (int part = 0; part < 4; ++part)
            m_OperationChoices[part] = m_OperationLocks[part] ? std::vector<CKGUID>{parts[part]} : distinct(fitting, part);
    } else if (!easy) {
        m_OperationChoices[3] = distinct(AvailableOperationSignatures(context, nullptr, &parts[2], &parts[0], &parts[1]), 3);
    }
    m_OperationValid = !AvailableOperationSignatures(context, &parts[3], &parts[2], &parts[0], &parts[1]).empty();
    m_OperationChoicesDirty = false;
}

void SchematicCanvas::OperationDialog() {
    const char *title = m_Doc.modifyOperation ? "Modify Parameter Operation###Operation" : "Add Parameter Operation###Operation";
    if (m_Doc.openOperationPicker) {
        ImGui::OpenPopup(title);
        m_Doc.openOperationPicker = false;
        m_OperationChoicesDirty = true;
    }
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (m_OperationChoicesDirty) RefreshOperationChoices();
    CKContext *context = m_State.document->Context();
    CKParameterManager *manager = context->GetParameterManager();
    const bool easy = m_State.preferences.Int("Schematic\\Edit Parameter Operation Mode") != 0;
    auto nameOf = [&](int part, const CKGUID &guid) -> std::string {
        if (part == 3) {
            const char *name = manager->OperationGuidToName(guid);
            return name ? AnsiToUtf8(name) : std::string("None");
        }
        if (guid == CKPGUID_NONE) return "None";
        CKParameterTypeDesc *desc = manager->GetParameterTypeDescription(guid);
        return desc ? AnsiToUtf8(desc->TypeName.CStr()) : std::string("?");
    };
    auto row = [&](int part, const char *id) {
        ImGui::PushID(id);
        if (easy) {
            if (ImGui::Checkbox("##lock", &m_OperationLocks[part])) m_OperationChoicesDirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock: the other lists keep what fits it");
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(m_OperationLocks[part]);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("##part", nameOf(part, m_OperationParts[part]).c_str(), ImGuiComboFlags_HeightLarge)) {
            TypeAhead typeAhead;
            for (const CKGUID &value : m_OperationChoices[part])
                if (typeAhead.Selectable(nameOf(part, value).c_str(), value == m_OperationParts[part])) {
                    m_OperationParts[part] = value;
                    m_OperationChoicesDirty = true;
                }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    };
    ImGui::TextUnformatted("Inputs");
    row(0, "input1");
    ImGui::SameLine();
    row(1, "input2");
    ImGui::TextUnformatted("Operation");
    row(3, "operation");
    ImGui::TextUnformatted("Output");
    row(2, "output");
    ImGui::Spacing();
    if (m_OperationValid) ImGui::TextUnformatted("Valid Parameter Operation");
    else ImGui::TextDisabled("Invalid Parameter Operation");
    ImGui::Spacing();
    ImGui::BeginDisabled(!m_OperationValid);
    if (ImGui::Button("OK", ImVec2(100, 0))) {
        // The inputs in the order the operation takes them (Dev tries both).
        const CKGUID *parts = m_OperationParts;
        const bool direct = !AvailableOperationSignatures(context, &parts[3], &parts[2], &parts[0], &parts[1], false).empty();
        OperationSignature signature;
        signature.operation = parts[3];
        signature.result = parts[2];
        signature.input1 = direct ? parts[0] : parts[1];
        signature.input2 = direct ? parts[1] : parts[0];
        if (m_Doc.modifyOperation) {
            ReplaceOperation(m_Doc.dialogScript, m_Doc.modifyOperation, signature);
        } else {
            Report(AddParameterOperation(*m_State.document, m_Doc.dialogScript, m_Doc.dialogGraph,
                                         signature.operation, signature.result, signature.input1, signature.input2,
                                         m_Doc.dialogPosition),
                   "Add parameter operation");
        }
        m_Doc.modifyOperation = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_Doc.modifyOperation = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Dev's Edit Parameters (Dev.exe sub_582930): OK and Apply write the
// values changed as one undo step; Cancel leaves them.
void SchematicCanvas::ParametersDialog() {
    const char *id = "###EditParameters";
    if (m_Doc.openParameters) {
        ImGui::OpenPopup(id);
        m_Doc.openParameters = false;
    }
    const DialogAction action = EditParametersDialog(id, m_ParametersTitle, m_ParameterBox, m_ParametersApply);
    if (action == DialogAction::Ok || action == DialogAction::Apply) {
        EditGroup group(*m_State.document, "Edit Parameters");
        m_ParameterBox.Apply(m_Doc.dialogScript);
    }
    if (action == DialogAction::Ok || action == DialogAction::Cancel) m_ParameterBox.Clear();
}

// Dev's Edit Parameter (Dev.exe sub_589D50, VIDialog margins 10, padding 6,
// rows 16 high and 5 apart): "Parameter Name :" and the name (right
// aligned, 165 wide), "Parameter Type :" and the type, then the value's row
// (or "Parameter Value :" and its text when it cannot be edited here).
void SchematicCanvas::ValueDialog() {
    const char *id = "###EditParameter";
    if (m_Doc.openValue) {
        ImGui::OpenPopup(id);
        m_Doc.openValue = false;
    }
    if (!ImGui::IsPopupOpen(id)) return;
    CKContext *context = m_State.document->Context();
    CKObject *edited = context->GetObject(m_Doc.valueParameter);
    CKParameterManager *manager = context->GetParameterManager();
    const float s = ImGui::GetFontSize() / 13.0f;
    const ImVec2 box = m_ParameterBox.Size(s, false);
    const float rowsWidth = std::max(265.0f * s, box.x + 2.0f * s);
    const float width = rowsWidth + 2.0f * 6.0f * s + 20.0f * s;
    const float X = 16.0f * s, right = width - 16.0f * s, field = 165.0f * s;
    float y = 11.0f * s;
    const float nameY = y;
    y += 21.0f * s;
    const float typeY = y;
    y += 21.0f * s;
    const bool shown = m_Doc.valueHolder == 0;
    const float valueY = y;
    if (shown) y += 21.0f * s;
    const float rowY = y;
    if (!m_ParameterBox.Empty()) y += box.y + 2.0f * s + 5.0f * s;
    const float panelBottom = y + 1.0f * s;
    const ImVec2 client(width, panelBottom + 4.0f * s + 18.0f * s + 5.0f * s);
    ImVec2 o;
    if (!vi::BeginDialog(id, "Edit Parameter", client, s, o)) return;
    vi::Panel(ImVec2(o.x + 10.0f * s, o.y + 5.0f * s), ImVec2(o.x + width - 10.0f * s, o.y + panelBottom), s);
    auto P = [&](float px, float py) { return ImVec2(o.x + px, o.y + py); };
    // The name.
    vi::Text(P(X, nameY + 2.0f * s), P(X + 120.0f * s, nameY + 16.0f * s), "Parameter Name :", true);
    if (m_ValueNameEditable) vi::Edit("##name", P(right - field, nameY), P(right, nameY + 16.0f * s), m_ValueName, vi::RIGHT);
    else vi::Text(P(right - field, nameY + 3.0f * s), P(right, nameY + 16.0f * s), m_ValueName.c_str(), false, vi::RIGHT);
    // The type: the types shown, by name (CBS_SORT).
    vi::Text(P(X, typeY + 2.0f * s), P(X + 120.0f * s, typeY + 16.0f * s), "Parameter Type :", true);
    const CKParameterType type = manager->ParameterGuidToType(m_ValueType);
    const char *typeName = type >= 0 ? manager->ParameterTypeToName(type) : nullptr;
    if (m_ValueTypeEditable) {
        std::vector<std::pair<std::string, CKGUID>> types;
        const int chosen = vi::Combo("##type", P(right - field, typeY), P(right, typeY + 16.0f * s),
                                     AnsiToUtf8(typeName ? typeName : ""),
                                     [&] {
                                         for (int t = 0; t < manager->GetParameterTypesCount(); ++t) {
                                             if (!manager->IsParameterTypeToBeShown(t)) continue;
                                             const char *n = manager->ParameterTypeToName(t);
                                             if (n && *n) types.emplace_back(AnsiToUtf8(n), manager->ParameterTypeToGuid(t));
                                         }
                                         std::sort(types.begin(), types.end(), [](const auto &a, const auto &b) {
                                             return _stricmp(a.first.c_str(), b.first.c_str()) < 0;
                                         });
                                         std::vector<std::string> names;
                                         for (const auto &entry : types) names.push_back(entry.first);
                                         return names;
                                     },
                                     -1);
        if (chosen >= 0 && chosen < static_cast<int>(types.size()) && !(types[chosen].second == m_ValueType)) {
            // Dev shows the new type's UI at once (ParamEditDlg::OnParameterTypeChange).
            m_ValueType = types[chosen].second;
            m_ParameterBox.Clear();
            if (CKParameter *holder = CKParameter::Cast(context->GetObject(m_Doc.valueHolder)))
                m_ParameterBox.Add(holder, Utf8ToAnsi(m_ValueName), m_ValueType);
        }
    } else {
        vi::Text(P(right - field, typeY + 3.0f * s), P(right, typeY + 16.0f * s), typeName ? typeName : "Unknown Type");
    }
    // The value: shown, or its row in a black frame.
    if (shown) {
        vi::Text(P(X, valueY + 2.0f * s), P(X + 120.0f * s, valueY + 16.0f * s), "Parameter Value :", true);
        CKParameter *value = CKParameter::Cast(edited);
        if (CKParameterIn *input = CKParameterIn::Cast(edited)) value = input->GetRealSource();
        const std::string text = value ? AnsiToUtf8(ParameterText(value)) : std::string("NA");
        vi::Text(P(right - field, valueY + 3.0f * s), P(right, valueY + 16.0f * s), text.c_str(), false, vi::RIGHT);
    }
    if (!m_ParameterBox.Empty()) {
        m_ParameterBox.Draw(P(X + 1.0f * s, rowY + 1.0f * s), s, false);
        ImGui::GetWindowDrawList()->AddRect(P(X, rowY), P(X + box.x + 2.0f * s, rowY + box.y + 2.0f * s), vi::Color(0));
    }
    float bx = o.x + width - 10.0f * s - 60.0f * s;
    const float by = o.y + panelBottom + 4.0f * s;
    bool cancel = vi::DialogButton("Cancel", ImVec2(bx, by), s, ImGuiKey_Escape) && m_Doc.invalidLinks.empty();
    bx -= 70.0f * s;
    bool ok = vi::DialogButton("OK", ImVec2(bx, by), s, ImGuiKey_Enter);
    // A new type some links would not fit: asked first (Dev's "Invalid
    // Parameter Link"), the dialog kept open meanwhile.
    bool apply = ok && edited && m_Doc.invalidLinks.empty(); // not while the question waits
    if (apply && m_ValueTypeEditable && !m_InvalidLinksAnswered) {
        CKParameterIn *in = CKParameterIn::Cast(edited);
        const CKGUID now = in ? in->GetGUID() : CKParameter::Cast(edited) ? CKParameter::Cast(edited)->GetGUID() : CKGUID();
        if (!(now == m_ValueType)) {
            m_Doc.invalidLinks = IncompatibleParameterLinks(*m_State.document, m_Doc.dialogScript, m_Doc.valueParameter, m_ValueType);
            m_Doc.invalidLinksScript = m_Doc.dialogScript;
            if (!m_Doc.invalidLinks.empty()) apply = false;
        }
    }
    if (m_InvalidLinksAnswered) {
        // The answer: OK applies the edit with the links deleted, Cancel leaves everything.
        apply = m_InvalidLinksAccepted && edited;
        m_InvalidLinksAnswered = false;
        cancel = cancel || !apply;
        ok = apply;
    }
    if (apply) {
        // The links that will not fit, the name, the type, then the value: one undo step.
        EditGroup group(*m_State.document, "Edit Parameter");
        if (!m_Doc.invalidLinks.empty()) {
            Report(DeleteElements(*m_State.document, m_Doc.invalidLinksScript, m_Doc.invalidLinks), "Delete Invalid Links");
            m_Doc.invalidLinks.clear();
        }
        const std::string name = Utf8ToAnsi(m_ValueName);
        if (m_ValueNameEditable && name != (edited->GetName() ? edited->GetName() : ""))
            RenameObject(*m_State.document, m_Doc.dialogScript, m_Doc.valueParameter, name);
        CKParameterIn *input = CKParameterIn::Cast(edited);
        const CKGUID current =
            input ? input->GetGUID() : CKParameter::Cast(edited) ? CKParameter::Cast(edited)->GetGUID() : CKGUID();
        if (m_ValueTypeEditable && !(current == m_ValueType)) {
            const EditResult typed = SetParameterType(*m_State.document, m_Doc.valueParameter, m_ValueType);
            if (!typed.ok) Report(typed, "Edit Parameter Type");
        }
        m_ParameterBox.Apply(m_Doc.dialogScript);
    }
    InvalidLinksDialog(); // over this dialog
    if ((ok && apply) || cancel) {
        m_Doc.invalidLinks.clear();
        m_ParameterBox.Clear();
        ImGui::CloseCurrentPopup();
    }
    vi::EndDialog();
}

// Opened over Edit Parameter (its OK waits for the answer).
void SchematicCanvas::InvalidLinksDialog() {
    if (m_Doc.invalidLinks.empty() || m_InvalidLinksAnswered) return;
    if (!ImGui::IsPopupOpen("Invalid Parameter Link")) ImGui::OpenPopup("Invalid Parameter Link");
    if (!ImGui::BeginPopupModal("Invalid Parameter Link", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("Parameter Type is incompatible with Parameter Input Type.\nThis link must be deleted.");
    if (m_Doc.invalidLinks.size() > 1) ImGui::Text("(%d links)", static_cast<int>(m_Doc.invalidLinks.size()));
    // Not the Enter that opened it (Edit Parameter's OK).
    const bool keys = !ImGui::IsWindowAppearing();
    if (ImGui::Button("OK", ImVec2(80, 0)) || (keys && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        m_InvalidLinksAnswered = true;
        m_InvalidLinksAccepted = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0)) || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        m_InvalidLinksAnswered = true;
        m_InvalidLinksAccepted = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void SchematicCanvas::EditElement(CK_ID script, const ElementRef &ref) {
    if (!m_State.HasDocument()) return;
    CKContext *context = m_State.document->Context();
    switch (ref.kind) {
    case ElementKind::Behavior: {
        m_State.selection.Clear();
        m_State.selection.Add(script, ref);
        if (!BehaviorSettings(CKBehavior::Cast(context->GetObject(ref.object))).empty()) OpenSettingsDialog();
        else OpenParametersDialog();
        break;
    }
    case ElementKind::Operation: OpenParametersOf(script, ref.object); break;
    case ElementKind::Pin:
        if (ref.pin == PinKind::ParamIn || ref.pin == PinKind::ParamOut) OpenParametersOf(script, ref.object);
        break;
    case ElementKind::LocalParam:
    case ElementKind::Shortcut: OpenValueDialog(script, context->GetObject(ref.object)); break;
    case ElementKind::Comment:
        if (ScriptView *view = ViewFor(script))
            for (const auto &comment : view->model.comments)
                if (comment.ref == ref) {
                    m_Doc.commentScript = script;
                    m_Doc.commentRef = comment.ref;
                    m_CommentText = comment.text;
                    m_Doc.openCommentEditor = true;
                }
        break;
    default: break;
    }
}

void SchematicCanvas::OpenSettingsDialog() {
    const Selection &selection = m_State.selection;
    if (selection.items.size() != 1 || selection.items[0].kind != ElementKind::Behavior || !m_State.HasDocument())
        return;
    CKBehavior *behavior = CKBehavior::Cast(m_State.document->Context()->GetObject(selection.items[0].object));
    m_Doc.settings = BehaviorSettings(behavior);
    if (m_Doc.settings.empty()) {
        m_State.Log(LogLevel::Info, "This behavior has no settings");
        return;
    }
    m_Doc.dialogScript = selection.script;
    m_Doc.settingsBehavior = behavior->GetID();
    m_Doc.settingsTexts.clear();
    // Dev's parameter box in mode 16 (Dev.exe sub_582930): a row per
    // setting, the local's own UI.
    m_ParameterBox.Clear();
    m_Doc.settingsSlots.clear();
    for (const auto &setting : m_Doc.settings)
        if (m_ParameterBox.Add(behavior->GetLocalParameter(setting.slot), setting.name)) m_Doc.settingsSlots.push_back(setting.slot);
    m_Doc.openSettings = true;
}

// Dev's "%s - Edit Settings" (Dev.exe sub_582930 in mode 16): the
// settings' rows; OK applies them together (one CKM_BEHAVIORSETTINGSEDITED).
void SchematicCanvas::SettingsDialog() {
    CKContext *context = m_State.HasDocument() ? m_State.document->Context() : nullptr;
    CKBehavior *behavior = context ? CKBehavior::Cast(context->GetObject(m_Doc.settingsBehavior)) : nullptr;
    const char *id = "###Settings";
    if (m_Doc.openSettings) {
        ImGui::OpenPopup(id);
        m_Doc.openSettings = false;
    }
    const std::string title = (behavior ? AnsiToUtf8(behavior->GetName() ? behavior->GetName() : "") : std::string()) +
                              " - Edit Settings";
    const DialogAction action = EditParametersDialog(id, title, m_ParameterBox, false);
    if (action == DialogAction::Ok && behavior) {
        m_Doc.settingsTexts.clear();
        for (size_t i = 0; i < m_Doc.settingsSlots.size() && i < m_ParameterBox.Count(); ++i)
            m_Doc.settingsTexts[m_Doc.settingsSlots[i]] = m_ParameterBox.Text(i);
        Report(EditBehaviorSettings(*m_State.document, m_Doc.dialogScript, m_Doc.settingsBehavior, m_Doc.settingsTexts),
               "Edit Settings");
    }
    if (action == DialogAction::Ok || action == DialogAction::Cancel) m_ParameterBox.Clear();
}

void SchematicCanvas::PriorityDialog() {
    if (m_OpenPriority) {
        ImGui::OpenPopup("Priority");
        m_OpenPriority = false;
    }
    if (!ImGui::BeginPopupModal("Priority", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputInt("Priority", &m_PriorityValue);
    if (ImGui::Button("OK", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        if (!m_PriorityScripts.empty()) {
            EditGroup group(*m_State.document, "Set Priority");
            for (CK_ID script : m_PriorityScripts)
                Report(SetBehaviorPriority(*m_State.document, script, script, m_PriorityValue), "Priority");
        } else {
            for (const auto &item : m_State.selection.items)
                if (item.kind == ElementKind::Behavior || item.kind == ElementKind::Script)
                    Report(SetBehaviorPriority(*m_State.document, m_Doc.dialogScript, item.object, m_PriorityValue),
                           "Priority");
        }
        m_PriorityScripts.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

bool SchematicCanvas::BlockCategoryMenu(CKGUID &chosen) {
    const BuildingBlockCatalog &catalog = m_State.catalog;
    bool picked = false;
    std::function<void(const BuildingBlockCategory &)> menu = [&](const BuildingBlockCategory &node) {
        for (const auto &child : node.children)
            if (ImGui::BeginMenu(AnsiToUtf8(child.first).c_str())) {
                menu(child.second);
                ImGui::EndMenu();
            }
        for (size_t index : node.entries) {
            const BuildingBlockEntry &entry = catalog.Entries()[index];
            if (ImGui::MenuItem(AnsiToUtf8(entry.name).c_str())) {
                chosen = entry.guid;
                picked = true;
            }
        }
    };
    menu(catalog.CategoryTree());
    return picked;
}

void SchematicCanvas::BlockMenus() {
    if (m_Doc.openBlockMenu) {
        ImGui::OpenPopup("BlockCategories");
        m_Doc.openBlockMenu = false;
    }
    if (m_Doc.openRecentMenu) {
        ImGui::OpenPopup("RecentBlocks");
        m_Doc.openRecentMenu = false;
    }
    const BuildingBlockCatalog &catalog = m_State.catalog;
    CKGUID chosen;
    bool picked = false;
    PushDevMenuStyle();
    if (ImGui::BeginPopup("BlockCategories")) {
        picked = BlockCategoryMenu(chosen);
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
    PushDevMenuStyle();
    if (ImGui::BeginPopup("RecentBlocks")) {
        if (m_State.recentBlocks.empty()) ImGui::TextDisabled("No Building Block inserted yet");
        for (const CKGUID &guid : m_State.recentBlocks)
            if (const BuildingBlockEntry *entry = catalog.Find(guid))
                if (ImGui::MenuItem(AnsiToUtf8(entry->name).c_str())) {
                    chosen = guid;
                    picked = true;
                }
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
    if (picked && m_Doc.replaceBehavior) OpenReplaceBlock(m_Doc.blockMenuScript, m_Doc.replaceBehavior, chosen);
    else if (picked) InsertBlockAt(chosen, m_Doc.blockMenuScript, m_Doc.blockMenuLocal);
    // A menu closed without a choice no longer replaces.
    if (!picked && !ImGui::IsPopupOpen("BlockCategories") && !ImGui::IsPopupOpen("RecentBlocks") &&
        !ImGui::IsPopupOpen("Replace Building Block") && !m_Doc.openReplaceBlock)
        m_Doc.replaceBehavior = 0;
}

} // namespace vdev
