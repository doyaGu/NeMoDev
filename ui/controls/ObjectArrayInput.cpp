#include "ui/controls/ObjectArrayInput.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/setup/SetupLayout.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>

namespace vdev {
uint32_t ObjectArrayInput::Draw(const EditorHost *host, const EditorValueInput &input) {
    if (input.value.type != EDITOR_OBJECT_LIST || input.value.objects.count > 65536 ||
        (input.value.objects.count && !input.value.objects.data))
        return EDITOR_INPUT_INVALID;
    std::vector<ObjectRef> objects;
    if (input.value.objects.count)
        objects.assign(input.value.objects.data, input.value.objects.data + input.value.objects.count);
    EditorClient client(host, input.choicesRequest.scope.view);
    const auto at = ImGui::GetCursorScreenPos();
    const auto s = ImGui::GetFontSize() / 13.f;
    const ImVec2 listMin(at.x + 15 * 1.5f * s, at.y + 5 * 1.625f * s),
        listMax(listMin.x + 195 * 1.5f * s, listMin.y + 95 * 1.625f * s);
    auto *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(listMin, listMax, vi::Color(9));
    dl->AddRect(listMin, listMax, vi::Color(0));
    const float widths[] = {120 * s, 40 * s, 80 * s, 35 * s}, header = 16 * s, line = 15 * s;
    const char *headers[] = {"Name", "ID", "Class", "Dyn"};
    dl->PushClipRect({listMin.x + 1, listMin.y + 1}, {listMax.x - 1, listMax.y - 1}, true);
    float x = listMin.x + 1;
    for (int c = 0; c < 4; ++c) {
        const ImVec2 a(x, listMin.y + 1), b(x + widths[c], listMin.y + 1 + header);
        dl->AddRectFilled(a, b, vi::Color(7));
        dl->AddLine({b.x - 1, a.y}, {b.x - 1, b.y}, vi::Color(4));
        dl->AddLine({a.x, b.y - 1}, {b.x, b.y - 1}, vi::Color(4));
        const auto w = ImGui::CalcTextSize(headers[c]).x;
        const auto tx = c == 0 ? a.x + 4 * s : c == 1 ? b.x - w - 4 * s : (a.x + b.x - w) * .5f;
        dl->AddText({tx, a.y + (header - ImGui::GetFontSize()) * .5f}, vi::Color(0), headers[c]);
        x += widths[c];
    }
    dl->PopClipRect();
    const ImVec2 itemsMin(listMin.x + 1, listMin.y + 1 + header);
    ImGui::SetCursorScreenPos(itemsMin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
    ImGui::BeginChild("##items", {listMax.x - itemsMin.x - 1, listMax.y - itemsMin.y - 1});
    auto *items = ImGui::GetWindowDrawList();
    const auto &io = ImGui::GetIO();
    int menu = -1;
    bool changed = false;
    const auto summary = client.Query("parameter.array-objects", {ObjectsArgument("objects", objects)});
    const auto *summaries = EditorClient::Find(summary, "rows");
    for (int i = 0; i < int(objects.size()); ++i) {
        EditorResponse info;
        info.status = summary.status;
        if (summaries && size_t(i) < summaries->fields.size())
            info.fields = summaries->fields[size_t(i)].fields;
        const auto a = ImGui::GetCursorScreenPos();
        const ImVec2 b(a.x + widths[0] + widths[1] + widths[2] + widths[3], a.y + line);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##item", {b.x - a.x, line},
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemClicked(0) || (ImGui::IsItemClicked(1) && !m_Chosen.count(i))) {
            if (io.KeyShift && m_Anchor >= 0) {
                if (!io.KeyCtrl)
                    m_Chosen.clear();
                for (int k = std::min(m_Anchor, i); k <= std::max(m_Anchor, i); ++k)
                    m_Chosen.insert(k);
            } else if (io.KeyCtrl) {
                if (!m_Chosen.erase(i))
                    m_Chosen.insert(i);
                m_Anchor = i;
            } else {
                m_Chosen = {i};
                m_Anchor = i;
            }
            ImGui::SetKeyboardFocusHere(-1);
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && info.status == EDITOR_OK)
            client.Command("object.open-setup", {ObjectArgument("target", objects[size_t(i)])});
        if (ImGui::IsItemClicked(1))
            menu = i;
        ImGui::PopID();
        const bool selected = m_Chosen.count(i);
        if (selected) items->AddRectFilled(a, b, CurrentDevTheme().selectionFill);
        const auto ink = selected ? CurrentDevTheme().selectionText : vi::Color(0);
        const std::string cells[] = {EditorClient::Text(info, "name"), std::to_string(objects[size_t(i)].object),
                                     EditorClient::Text(info, "class"),
                                     EditorClient::Integer(info, "dynamic") ? "X" : "-"};
        float cx = a.x;
        for (int c = 0; c < 4; ++c) {
            const auto w = ImGui::CalcTextSize(cells[c].c_str()).x;
            const auto tx = c == 0 ? cx + 4 * s : c == 1 ? cx + widths[c] - w - 4 * s : cx + (widths[c] - w) * .5f;
            items->PushClipRect({cx, a.y}, {cx + widths[c] - 1, b.y}, true);
            items->AddText({tx, a.y + (line - ImGui::GetFontSize()) * .5f}, ink, cells[c].c_str());
            items->PopClipRect();
            cx += widths[c];
            items->AddLine({cx - 1, a.y}, {cx - 1, b.y}, vi::Color(7));
        }
        items->AddLine({a.x, b.y - 1}, {b.x, b.y - 1}, vi::Color(7));
    }
    const auto rest = ImGui::GetCursorScreenPos();
    const auto restHeight = std::max(1.f, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - rest.y);
    ImGui::InvisibleButton("##rest", {std::max(1.f, ImGui::GetContentRegionAvail().x), restHeight},
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemClicked(0))
        m_Chosen.clear();
    if (ImGui::IsItemClicked(1))
        menu = int(objects.size());
    const auto selection = [&] {
        std::vector<ObjectRef> result;
        for (auto i : m_Chosen)
            if (i >= 0 && i < int(objects.size()))
                result.push_back(objects[size_t(i)]);
        return result;
    };
    const auto copy = [&] {
        const auto values = selection();
        client.Command("scene.copy-references", {ObjectsArgument("objects", values)});
    };
    const auto paste = [&](int at) {
        const auto result = client.Query("scene.clipboard");
        if (const auto *values = EditorClient::Find(result, "objects");
            result.status == EDITOR_OK && values && !values->objects.empty()) {
            objects.insert(objects.begin() + std::clamp(at, 0, int(objects.size())), values->objects.begin(),
                           values->objects.end());
            changed = true;
            m_Chosen.clear();
        }
    };
    if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_Chosen.empty()) {
            for (auto i = m_Chosen.rbegin(); i != m_Chosen.rend(); ++i)
                if (*i >= 0 && *i < int(objects.size()))
                    objects.erase(objects.begin() + *i);
            m_Chosen.clear();
            changed = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !m_Chosen.empty())
            copy();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
            paste(int(objects.size()));
        const bool down = ImGui::IsKeyPressed(ImGuiKey_KeypadAdd), up = ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract);
        if ((down || up) && !m_Chosen.empty()) {
            const auto from = *m_Chosen.begin(), to = from + (down ? 1 : -1);
            if (to >= 0 && to < int(objects.size())) {
                std::swap(objects[size_t(from)], objects[size_t(to)]);
                m_Chosen = {to};
                m_Anchor = to;
                changed = true;
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (menu >= 0) {
        ImGui::OpenPopup("##arraymenu");
        m_Anchor = menu;
    }
    bool choose = false;
    PushDevMenuStyle();
    if (ImGui::BeginPopup("##arraymenu")) {
        if (ImGui::MenuItem("Add Object"))
            choose = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Copy Object(s)", nullptr, false, !m_Chosen.empty()))
            copy();
        if (ImGui::MenuItem("Paste Object(s)"))
            paste(m_Anchor);
        ImGui::EndPopup();
    }
    PopDevMenuStyle();
    const char *chooser = "Edit Parameter###ChooseObject";
    if (choose) {
        m_Object.object = {};
        m_Filter.clear();
        m_Derived = true;
        const auto classes = client.Query("parameter.object-classes");
        if (const auto *rows = EditorClient::Find(classes, "rows"))
            m_Classes = rows->fields;
        for (const auto &entry : m_Classes) {
            EditorResponse row;
            row.fields = entry.fields;
            if (EditorClient::Integer(row, "default"))
                m_Class = EditorClient::Integer(row, "id");
        }
        ImGui::OpenPopup(chooser);
    }
    uint32_t action = 0;
    if (ImGui::IsPopupOpen(chooser)) {
        const auto unit = setup::PageUnit();
        vi::DialogLayout layout;
        if (vi::BeginScrollableDialog(chooser, "Edit Parameter", {450 * unit, 112 * unit}, unit, layout)) {
            uint32_t edited = 0;
            if (vi::BeginDialogContent("##object-choice", layout)) {
                setup::SetupForm form("##object-choice-fields");
                if (form) {
                    form.Row("Object");
                    std::vector<EditorField> owned{m_Object};
                    const auto value = BorrowEditorFields(owned);
                    EditorValue target{EDITOR_TEXT("target"), EDITOR_OBJECT};
                    for (uint32_t i = 0; i < input.choicesRequest.count; ++i) {
                        const auto &field = input.choicesRequest.values[i];
                        if (field.type == EDITOR_OBJECT && std::string(field.key.data, field.key.size) == "target")
                            target.object = field.object;
                    }
                    const EditorValue args[] = {target, IntegerArgument("class", m_Class),
                                                IntegerArgument("derived", m_Derived ? 1 : 2),
                                                TextArgument("filter", m_Filter)};
                    EditorResponse details;
                    if (m_Object.object)
                        details = client.Query("object.details", {ObjectArgument("target", m_Object.object)});
                    const auto name = EditorClient::Text(details, "name");
                    const EditorValue display{EDITOR_TEXT("display"), EDITOR_TEXT_VALUE, {}, EditorText(name)};
                    const auto write = [](void *data, const EditorValue *v, uint32_t count) {
                        std::vector<EditorField> fields;
                        if (count != 1 || v->type != EDITOR_OBJECT || CopyEditorFields(fields, v, count) != EDITOR_OK)
                            return EDITOR_INVALID;
                        *static_cast<EditorField *>(data) = std::move(fields[0]);
                        return EDITOR_OK;
                    };
                    auto object = input;
                    object.label = EDITOR_TEXT("##object");
                    object.value = value.values.front();
                    object.fields = &display;
                    object.count = 1;
                    object.width = std::max(1.f, ImGui::GetContentRegionAvail().x);
                    object.height = 16 * unit;
                    object.drawMembers = nullptr;
                    object.membersData = nullptr;
                    object.update = {&m_Object, write};
                    object.choicesRequest = {client.Scope(), args, 4, input.choicesRequest.handler};
                    edited = host->drawValue(host->data, EditorString EDITOR_TEXT("value.object"), &object);
                    std::vector<std::string> names;
                    int selected = -1;
                    for (size_t i = 0; i < m_Classes.size(); ++i) {
                        EditorResponse row;
                        row.fields = m_Classes[i].fields;
                        names.push_back(EditorClient::Text(row, "name"));
                        if (EditorClient::Integer(row, "id") == m_Class)
                            selected = int(i);
                    }
                    form.Row("Class");
                    const auto classAt = ImGui::GetCursorScreenPos();
                    const auto picked = vi::Combo(
                        "##class", classAt, {classAt.x + std::max(1.f, ImGui::GetContentRegionAvail().x), classAt.y + 18 * unit},
                        selected >= 0 ? names[size_t(selected)] : "", [&] { return names; },
                        selected);
                    if (picked >= 0) {
                        EditorResponse row;
                        row.fields = m_Classes[size_t(picked)].fields;
                        m_Class = EditorClient::Integer(row, "id");
                    }
                    form.Row("Derived");
                    vi::Check("##derived", ImGui::GetCursorScreenPos(), 12 * unit, m_Derived);
                    form.Row("Text Filter");
                    const auto filterAt = ImGui::GetCursorScreenPos();
                    vi::Edit("##filter", filterAt,
                              {filterAt.x + std::max(1.f, ImGui::GetContentRegionAvail().x), filterAt.y + 18 * unit}, m_Filter,
                              vi::CENTER);
                }
            }
            vi::EndDialogContent();
            if (vi::DialogButton("OK", {layout.right - 130 * unit, layout.buttonsY}, unit, ImGuiKey_Enter) &&
                !(edited & EDITOR_INPUT_ACTIVE)) {
                if (m_Object.object) {
                    objects.insert(objects.begin() + std::clamp(m_Anchor, 0, int(objects.size())), m_Object.object);
                    m_Chosen.clear();
                    changed = true;
                }
                ImGui::CloseCurrentPopup();
            }
            if (vi::DialogButton("Cancel", {layout.right - 60 * unit, layout.buttonsY}, unit, ImGuiKey_Escape) &&
                !(edited & EDITOR_INPUT_ACTIVE))
                ImGui::CloseCurrentPopup();
            vi::EndDialog();
            action |= EDITOR_INPUT_ACTIVE;
        }
    }
    if (changed) {
        auto v = input.value;
        v.objects = {objects.data(), uint32_t(objects.size())};
        if (input.update.write(input.update.data, &v, 1) != EDITOR_OK)
            action |= EDITOR_INPUT_INVALID;
        action |= EDITOR_INPUT_CHANGED | EDITOR_INPUT_ACCEPT;
    }
    return action;
}
} // namespace vdev
