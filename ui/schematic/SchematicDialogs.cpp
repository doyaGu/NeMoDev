// Modal dialogs and pickers of the Schematic canvas.
#include "ui/schematic/SchematicCanvas.h"

#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/setup/SetupLayout.h"
#include "ui/controls/TypeAhead.h"
#include "workspace/EditorWorkspace.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <functional>
#include <map>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"


namespace vdev {

bool SchematicCanvas::BeginPropertyInput(const char *property, ObjectRef target,
                                         const std::vector<EditorField> &arguments) {
    ClosePropertyInput();
    return m_Workspace.Begin(property, target, m_PropertyEdit, m_PropertyOwner, arguments);
}
void SchematicCanvas::ClosePropertyInput() {
        m_Workspace.Finish(m_PropertyEdit, false);
}
void SchematicCanvas::CommentEditor() {
    if (m_Doc.openCommentEditor) {
        m_Doc.openCommentEditor = false;
        const auto &address = m_Doc.commentAddress;
        if (!address || !BeginPropertyInput("schematic.comment", address.script,
                                            {{"owner", EDITOR_OBJECT, address.owner},
                                             {"index", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(address.element.index)},
                                             {"revision", EDITOR_INTEGER_VALUE, {}, {}, address.revision},
                                             {"topology", EDITOR_INTEGER_VALUE, {}, {}, address.topology}}))
            return;
        ImGui::OpenPopup("Edit Comment");
    }
    if (!ImGui::IsPopupOpen("Edit Comment")) return;
    const auto unit = setup::PageUnit();
    vi::DialogLayout layout;
    if (!vi::BeginScrollableDialog("Edit Comment", "Edit Comment", {420 * unit, 180 * unit}, unit, layout)) return;
    const bool appearing = ImGui::IsWindowAppearing();
    if (vi::BeginDialogContent("##comment", layout)) {
        m_Workspace.DrawInput(m_PropertyEdit, "##text", appearing, nullptr, EDITOR_VALUE_DIALOG,
                              std::max(1.f, ImGui::GetContentRegionAvail().x),
                              std::max(160 * unit, ImGui::GetContentRegionAvail().y));
    }
    vi::EndDialogContent();
    ImGui::SetCursorScreenPos({layout.right - 210 * unit, layout.buttonsY});
    bool close = false;
    if (ImGui::Button("OK", ImVec2(100 * unit, 18 * unit))) close = m_Workspace.Finish(m_PropertyEdit, true);
    ImGui::SameLine(0, 10 * unit);
    if (ImGui::Button("Cancel", ImVec2(100 * unit, 18 * unit)) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        !m_PropertyEdit)
        close = true;
    if (close) {
        ClosePropertyInput();
        ImGui::CloseCurrentPopup();
    }
    vi::EndDialog();
}

void SchematicCanvas::CloseValueInputs() {
    ClosePropertyInput();
    m_Workspace.CloseOwner(m_PropertyOwner);
    m_PriorityTargets.clear();
    if (m_ParameterDialog)
        m_ParameterDialog->Close();
    {
        m_Workspace.Finish(m_RenameEdit, false);
        m_Workspace.CloseOwner(m_RenameOwner);
    }
    m_RenameTarget = {};
}
void SchematicCanvas::RenameDialog() {
    if (m_Doc.openRename) {
        m_Doc.openRename = false;
        if (!m_Workspace.Begin("object.name", m_RenameTarget, m_RenameEdit, m_RenameOwner)) return;
        ImGui::OpenPopup("Rename");
    }
    if (!ImGui::IsPopupOpen("Rename")) return;
    const auto unit = setup::PageUnit();
    vi::DialogLayout layout;
    if (!vi::BeginScrollableDialog("Rename", "Rename", {320 * unit, 40 * unit}, unit, layout)) return;
    const bool appearing = ImGui::IsWindowAppearing();
    bool entered = false;
    if (vi::BeginDialogContent("##property", layout)) {
        ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Name").x -
                                                  ImGui::GetStyle().ItemInnerSpacing.x));
        entered = m_Workspace.DrawInput(m_RenameEdit, "Name", appearing);
    }
    vi::EndDialogContent();
    ImGui::SetCursorScreenPos({layout.right - 210 * unit, layout.buttonsY});
    const bool accept = ImGui::Button("OK", ImVec2(100 * unit, 18 * unit)) || entered;
    ImGui::SameLine(0, 10 * unit);
    const bool cancel =
        ImGui::Button("Cancel", ImVec2(100 * unit, 18 * unit)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !m_RenameEdit;
    // A value can accept on deactivation when Cancel takes focus.
    // Render both buttons before deciding whether to keep the draft.
    const bool cancelling = ImGui::IsItemActive();
    bool close = false;
    if (cancel) {
        m_Workspace.Finish(m_RenameEdit, false);
        close = true;
    } else if (!cancelling && accept)
        close = m_Workspace.Finish(m_RenameEdit, true);
    if (close) ImGui::CloseCurrentPopup();
    vi::EndDialog();
}

void SchematicCanvas::OpenLinkDelay(uint32_t script, const ElementRef &ref) {
    ElementAddress address;
    if (auto *view = ViewFor(script)) {
        Hit hit;
        hit.view = view;
        hit.ref = ref;
        RetainHit(hit, address);
    }
    m_Doc.delayTarget = address.object;
    m_Doc.openDelay = bool(m_Doc.delayTarget);
}
void SchematicCanvas::DelayDialog() {
    if (m_Doc.openDelay) {
        m_Doc.openDelay = false;
        if (!m_Doc.delayTarget || !BeginPropertyInput("setup.behavior-link.delay", m_Doc.delayTarget))
            return;
        ImGui::OpenPopup("Link Delay");
    }
    if (!ImGui::IsPopupOpen("Link Delay")) return;
    const auto unit = setup::PageUnit();
    vi::DialogLayout layout;
    if (!vi::BeginScrollableDialog("Link Delay", "Link Delay", {320 * unit, 40 * unit}, unit, layout)) return;
    const bool appearing = ImGui::IsWindowAppearing();
    bool entered = false;
    if (vi::BeginDialogContent("##property", layout)) {
        ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Frames").x -
                                                  ImGui::GetStyle().ItemInnerSpacing.x));
        entered = m_Workspace.DrawInput(m_PropertyEdit, "Frames", appearing);
    }
    vi::EndDialogContent();
    ImGui::SetCursorScreenPos({layout.right - 210 * unit, layout.buttonsY});
    const bool accept =
        ImGui::Button("OK", ImVec2(100 * unit, 18 * unit)) || entered || ImGui::IsKeyPressed(ImGuiKey_Enter);
    ImGui::SameLine(0, 10 * unit);
    const bool cancel = ImGui::Button("Cancel", ImVec2(100 * unit, 18 * unit)) ||
                        ImGui::IsKeyPressed(ImGuiKey_Escape) || !m_PropertyEdit;
    const bool cancelling = ImGui::IsItemActive();
    bool close = cancel;
    if (!cancel && !cancelling && accept) close = m_Workspace.Finish(m_PropertyEdit, true);
    if (close) {
        ClosePropertyInput();
        ImGui::CloseCurrentPopup();
    }
    vi::EndDialog();
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
    if (!ImGui::IsPopupOpen("Add Parameter")) return;
    const auto unit = setup::PageUnit();
    vi::DialogLayout layout;
    if (!vi::BeginScrollableDialog("Add Parameter", "Add Parameter", {380 * unit, 320 * unit}, unit, layout)) return;
    if (vi::BeginDialogContent("##local-parameter", layout)) {
        ImGui::TextDisabled("%s", m_TypePurpose == TypePurpose::Local        ? "Local parameter"
                                  : m_TypePurpose == TypePurpose::GraphInput ? "Input parameter"
                                                                             : "Output parameter");
        ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Name").x -
                                                  ImGui::GetStyle().ItemInnerSpacing.x));
        ImGui::InputText("Name", &m_LocalName);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "Filter types", &m_PickerFilter);
        if (ImGui::BeginListBox("##types", {std::max(1.f, ImGui::GetContentRegionAvail().x),
                                            std::max(160 * unit, ImGui::GetContentRegionAvail().y)})) {
            TypeAhead typeAhead;
            for (int i = 0; i < static_cast<int>(m_Types.size()); ++i) {
                const std::string &name = m_Types[i].name;
                if (!MatchesFilter(name, m_PickerFilter)) continue;
                if (typeAhead.Selectable(name.c_str(), i == m_TypeIndex)) m_TypeIndex = i;
            }
            ImGui::EndListBox();
        }
    }
    vi::EndDialogContent();
    ImGui::SetCursorScreenPos({layout.right - 210 * unit, layout.buttonsY});
    const bool ready = m_TypeIndex >= 0 && m_TypeIndex < static_cast<int>(m_Types.size()) && !m_LocalName.empty();
    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("Add", ImVec2(100 * unit, 18 * unit))) {
        auto fields = m_Doc.creation;
        fields.push_back({"type", EDITOR_INTEGER_VALUE, {}, {}, m_Types[m_TypeIndex].type});
        fields.push_back({"name", EDITOR_TEXT_VALUE, {}, m_LocalName});
        if (m_TypePurpose == TypePurpose::Local) {
            ExecuteAuthoring("schematic.add-local", std::move(fields));
            m_Navigation.options.showLocalParams = true;
        } else {
            fields.push_back(
                {"kind",
                 EDITOR_INTEGER_VALUE,
                 {},
                 {},
                 uint64_t(m_TypePurpose == TypePurpose::GraphInput ? PinKind::ParamIn : PinKind::ParamOut)});
            ExecuteAuthoring("schematic.add-pin", std::move(fields));
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 10 * unit);
    if (ImGui::Button("Cancel", ImVec2(100 * unit, 18 * unit)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    vi::EndDialog();
}

// Dev's Add Parameter Operation dialog (Dev.exe sub_595220): Inputs (two
// types), Operation and Output, each with its lock in the Easy Finding mode
// (Edit Parameter Operation Mode 1; a locked part narrows the others' lists
// to what fits it, sub_5969F0); in the Quick Finding mode (0, 1.0.1 style)
// the Operation list holds what the three types allow (sub_595CC0). The
// line below tells whether the four make an operation (sub_5977D0: language
// strings 634 / 635). The parts stay from one opening to the next, as Dev's.
void SchematicCanvas::RefreshOperationChoices() {
    const bool easy = m_Preferences.Int("Schematic\\Edit Parameter Operation Mode") != 0;
    if (!easy)
        for (auto &lock : m_OperationLocks)
            lock = false;
    EditorField parts{"parts", EDITOR_INTEGER_LIST}, locks{"locks", EDITOR_INTEGER_LIST};
    parts.integers.assign(m_OperationParts, m_OperationParts + 4);
    for (auto lock : m_OperationLocks)
        locks.integers.push_back(lock);
    const std::vector<EditorField> fields{parts, locks, {"easy", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(easy)}};
    const auto values = BorrowEditorFields(fields);
    const auto result = Client().Query("schematic.operation-choices", {values.data(), values.data() + values.size()});
    m_OperationValid = result.status == EDITOR_OK && EditorClient::Integer(result, "valid");
    for (auto &choices : m_OperationChoices)
        choices.clear();
    if (const auto *choices = EditorClient::Find(result, "choices"))
        for (size_t i = 0; i < choices->fields.size() && i < 4; ++i)
            for (const auto &choice : choices->fields[i].fields) {
                EditorResponse item;
                item.fields = choice.fields;
                m_OperationChoices[i].push_back(
                    {EditorClient::Integer(item, "type"), EditorClient::Text(item, "name")});
            }
    if (const auto *names = EditorClient::Find(result, "names"))
        for (size_t i = 0; i < names->texts.size() && i < 4; ++i)
            m_OperationNames[i] = names->texts[i];
    m_OperationChoicesDirty = false;
}

void SchematicCanvas::OperationDialog() {
    const char *title = m_Doc.modifyOperation ? "Modify Parameter Operation###Operation" : "Add Parameter Operation###Operation";
    if (m_Doc.openOperationPicker) {
        ImGui::OpenPopup(title);
        m_Doc.openOperationPicker = false;
        m_OperationChoicesDirty = true;
    }
    if (!ImGui::IsPopupOpen(title)) return;
    const auto unit = setup::PageUnit();
    vi::DialogLayout layout;
    if (!vi::BeginScrollableDialog(title,
                                   m_Doc.modifyOperation ? "Modify Parameter Operation" : "Add Parameter Operation",
                                   {480 * unit, 220 * unit}, unit, layout))
        return;
    if (vi::BeginDialogContent("##operation-fields", layout)) {
        if (m_OperationChoicesDirty) RefreshOperationChoices();
        const bool easy = m_Preferences.Int("Schematic\\Edit Parameter Operation Mode") != 0;
        auto row = [&](int part, const char *id) {
            ImGui::PushID(id);
            if (easy) {
                if (ImGui::Checkbox("##lock", &m_OperationLocks[part])) m_OperationChoicesDirty = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock: the other lists keep what fits it");
                ImGui::SameLine(0, 10 * unit);
            }
            ImGui::BeginDisabled(m_OperationLocks[part]);
            ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x));
            if (ImGui::BeginCombo("##part", m_OperationNames[part].c_str(), ImGuiComboFlags_HeightLarge)) {
                TypeAhead typeAhead;
                for (const auto &value : m_OperationChoices[part])
                    if (typeAhead.Selectable(value.name.c_str(), value.type == m_OperationParts[part])) {
                        m_OperationParts[part] = value.type;
                        m_OperationChoicesDirty = true;
                    }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        };
        {
            setup::SetupForm form("##operation-parts");
            if (form) {
                form.Row("First Input");
                row(0, "input1");
                form.Row("Second Input");
                row(1, "input2");
                form.Row("Operation");
                row(3, "operation");
                form.Row("Output");
                row(2, "output");
            }
        }
        ImGui::Spacing();
        if (m_OperationValid)
            ImGui::TextUnformatted("Valid Parameter Operation");
        else
            ImGui::TextDisabled("Invalid Parameter Operation");
        ImGui::Spacing();
    }
    vi::EndDialogContent();
    ImGui::SetCursorScreenPos({layout.right - 210 * unit, layout.buttonsY});
    ImGui::BeginDisabled(!m_OperationValid);
    if (ImGui::Button("OK", ImVec2(100 * unit, 18 * unit))) {
        auto fields = m_Doc.creation;
        EditorField parts{"parts", EDITOR_INTEGER_LIST};
        parts.integers.assign(m_OperationParts, m_OperationParts + 4);
        fields.push_back(std::move(parts));
        if (m_Doc.modifyOperation)
            fields.push_back({"operation", EDITOR_OBJECT, m_Doc.modifyOperation});
        ExecuteAuthoring("schematic.edit-operation", std::move(fields));
        m_Doc.modifyOperation = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 10 * unit);
    if (ImGui::Button("Cancel", ImVec2(100 * unit, 18 * unit)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_Doc.modifyOperation = {};
        ImGui::CloseCurrentPopup();
    }
    vi::EndDialog();
}

// Dev's Edit Parameters (Dev.exe sub_582930): OK and Apply write the
// values changed as one undo step; Cancel leaves them.
void SchematicCanvas::ParametersDialog() {
    if (!m_ParameterDialog)
        return;
    m_ParameterDialog->Draw();
    const auto message = m_ParameterDialog->TakeMessage();
    if (!message.empty())
        Client().Log(EDITOR_LOG_WARNING, message);
}

// Dev's Edit Parameter (Dev.exe sub_589D50, VIDialog margins 10, padding 6,
// rows 16 high and 5 apart): "Parameter Name :" and the name (right
// aligned, 165 wide), "Parameter Type :" and the type, then the value's row
// (or "Parameter Value :" and its text when it cannot be edited here).

// Opened over Edit Parameter (its OK waits for the answer).

void SchematicCanvas::EditElement(uint32_t script, const ElementRef &ref) {
    const auto info = ElementInfo(script, ref);
    const auto *object = EditorClient::Find(info, "object");
    switch (ref.kind) {
    case ElementKind::Behavior:
        if (info.status != EDITOR_OK)
            return;
        m_Navigation.selection.Set(script, ref);
        if (EditorClient::Integer(info, "settings"))
            OpenSettingsDialog();
        else
            OpenParametersDialog();
        break;
    case ElementKind::Operation: OpenParametersOf(script, ref.object); break;
    case ElementKind::Pin:
        if (ref.pin == PinKind::ParamIn || ref.pin == PinKind::ParamOut)
            OpenParametersOf(script, ref.object);
        break;
    case ElementKind::LocalParam:
    case ElementKind::Shortcut:
        if (info.status == EDITOR_OK && object)
            OpenValueDialog(script, object->object);
        break;
    case ElementKind::Comment:
        if (auto *view = ViewFor(script)) {
            Hit hit;
            hit.view = view;
            hit.ref = ref;
            RetainHit(hit, m_Doc.commentAddress);
        }
        m_Doc.openCommentEditor = bool(m_Doc.commentAddress);
        break;
    default: break;
    }
}

void SchematicCanvas::OpenSettingsDialog() {
    const auto &selection = m_Navigation.selection;
    if (selection.items.size() != 1 || selection.items.front().kind != ElementKind::Behavior)
        return;
    const auto info = ElementInfo(selection.script, selection.items.front());
    const auto *object = EditorClient::Find(info, "object");
    if (info.status != EDITOR_OK || !object)
        return;
    if (!EditorClient::Integer(info, "settings")) {
        Client().Log(EDITOR_LOG_INFO, "This behavior has no settings");
        return;
    }
    const auto scripts = ScriptReferences({selection.script});
    if (!scripts.empty())
        OpenParameterEditor(scripts.front(), object->object, 2);
}

void SchematicCanvas::PriorityDialog() {
    if (m_OpenPriority) {
        m_OpenPriority = false;
        if (m_PriorityTargets.empty()) return;
        EditorField targets{"targets", EDITOR_OBJECT_LIST};
        targets.objects = m_PriorityTargets;
        if (!BeginPropertyInput("setup.behavior.priority", targets.objects.front(), {targets})) return;
        ImGui::OpenPopup("Priority");
    }
    if (!ImGui::IsPopupOpen("Priority")) return;
    const auto unit = setup::PageUnit();
    vi::DialogLayout layout;
    if (!vi::BeginScrollableDialog("Priority", "Priority", {320 * unit, 40 * unit}, unit, layout)) return;
    const bool appearing = ImGui::IsWindowAppearing();
    bool entered = false;
    if (vi::BeginDialogContent("##property", layout)) {
        ImGui::SetNextItemWidth(std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Priority").x -
                                                  ImGui::GetStyle().ItemInnerSpacing.x));
        entered = m_Workspace.DrawInput(m_PropertyEdit, "Priority", appearing);
    }
    vi::EndDialogContent();
    ImGui::SetCursorScreenPos({layout.right - 210 * unit, layout.buttonsY});
    const bool accept =
        ImGui::Button("OK", ImVec2(100 * unit, 18 * unit)) || entered || ImGui::IsKeyPressed(ImGuiKey_Enter);
    ImGui::SameLine(0, 10 * unit);
    const bool cancel = ImGui::Button("Cancel", ImVec2(100 * unit, 18 * unit)) ||
                        ImGui::IsKeyPressed(ImGuiKey_Escape) || !m_PropertyEdit;
    const bool cancelling = ImGui::IsItemActive();
    bool close = cancel;
    if (!cancel && !cancelling && accept) close = m_Workspace.Finish(m_PropertyEdit, true);
    if (close) {
        ClosePropertyInput();
        m_PriorityTargets.clear();
        ImGui::CloseCurrentPopup();
    }
    vi::EndDialog();
}

bool SchematicCanvas::BlockCategoryMenu(uint64_t &chosen) {
    m_BlockCatalog.Refresh(Client());
    const auto &catalog = m_BlockCatalog;
    bool picked = false;
    std::function<void(const BuildingBlockCategories &)> menu = [&](const BuildingBlockCategories &node) {
        for (const auto &child : node.children)
            if (ImGui::BeginMenu(child.first.c_str())) {
                menu(child.second);
                ImGui::EndMenu();
            }
        for (size_t index : node.entries) {
            const auto &entry = catalog.Entries()[index];
            if (ImGui::MenuItem(entry.name.c_str())) {
                chosen = entry.prototype;
                picked = true;
            }
        }
    };
    menu(catalog.Categories());
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
    if (!ImGui::IsPopupOpen("BlockCategories") && !ImGui::IsPopupOpen("RecentBlocks")) {
        if (!ImGui::IsPopupOpen("Replace Building Block") && !m_Doc.openReplaceBlock &&
            !ImGui::IsPopupOpen("Replace with") && !m_Doc.openReplaceGraph)
            m_Doc.replaceBehavior = {};
        return;
    }
    m_BlockCatalog.Refresh(Client());
    const auto &catalog = m_BlockCatalog;
    uint64_t chosen = 0;
    bool picked = false;
    PushDevMenuStyle();
    if (ImGui::BeginPopup("BlockCategories")) {
        picked = BlockCategoryMenu(chosen);
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
    PushDevMenuStyle();
    if (ImGui::BeginPopup("RecentBlocks")) {
        const auto recent = RecentBlocks();
        if (recent.empty())
            ImGui::TextDisabled("No Building Block inserted yet");
        for (const auto prototype : recent)
            if (const auto *entry = catalog.Find(prototype))
                if (ImGui::MenuItem(entry->name.c_str())) {
                    chosen = prototype;
                    picked = true;
                }
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
    if (picked && m_Doc.replaceBehavior)
        OpenReplaceBlock(m_Doc.replaceScript, m_Doc.replaceBehavior, chosen);
    else if (picked && !m_Doc.blockCreation.empty()) {
        auto fields = m_Doc.blockCreation;
        fields.push_back({"prototype", EDITOR_INTEGER_VALUE, {}, {}, chosen});
        ExecuteAuthoring("schematic.insert-block", std::move(fields), GraphFollow::Block);
    }
    // A menu closed without a choice no longer replaces.
    if (!picked && !ImGui::IsPopupOpen("BlockCategories") && !ImGui::IsPopupOpen("RecentBlocks") &&
        !ImGui::IsPopupOpen("Replace Building Block") && !m_Doc.openReplaceBlock &&
        !ImGui::IsPopupOpen("Replace with") && !m_Doc.openReplaceGraph)
        m_Doc.replaceBehavior = {};
}

} // namespace vdev
