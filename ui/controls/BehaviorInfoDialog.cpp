#include "ui/controls/BehaviorInfoDialog.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include "core/runtime/TextEncoding.h"

namespace vdev {

bool DrawBehaviorInfoFields(BehaviorInfoFields &fields, bool hideRepresentation) {
    ImGui::Text("Behavior :  %s", fields.name.c_str());
    ImGui::Text("Applies to Object of Type :  %s", fields.type.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Author :");
    ImGui::SameLine(90);
    ImGui::SetNextItemWidth(190);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##author", &fields.author);
    ImGui::SameLine();
    ImGui::TextUnformatted("Version :");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::InputText("##version", &fields.version, ImGuiInputTextFlags_CharsScientific);
    ImGui::TextUnformatted("Description");
    ImGui::InputTextMultiline("##description", &fields.description, ImVec2(510, 120));
    if (hideRepresentation) ImGui::Checkbox("Hide script representation in schematic view", &fields.hide);
    char *end = nullptr;
    const float version = std::strtof(fields.version.c_str(), &end);
    return fields.version.empty() || (std::isfinite(version) && end == fields.version.c_str() + fields.version.size());
}

void BehaviorInfoDialog::Open(const std::string &nms, const std::string &name, CK_CLASSID cid) {
    m_Path = nms;
    m_Description = BehaviorDescription();
    // The .dsc there, then the behavior's name and class (sub_581FB0).
    const bool read = ReadBehaviorDescription(nms, m_Description);
    if (!name.empty()) m_Description.name = name;
    m_Fields.name = !name.empty() || read ? AnsiToUtf8(m_Description.name) : std::filesystem::u8path(nms).stem().u8string();
    if (cid) m_Description.cid = cid;
    m_Fields.author = AnsiToUtf8(m_Description.author);
    m_Fields.description = AnsiToUtf8(m_Description.description);
    m_Fields.version.clear();
    if (read) {
        char text[64];
        std::snprintf(text, sizeof(text), "%g", m_Description.version);
        m_Fields.version = text;
    }
    const auto *type = m_Description.cid ? CKClassIDToString(m_Description.cid) : nullptr;
    m_Fields.type = type ? AnsiToUtf8(type) : "";
    m_Written = false;
    m_Open = true;
}

DialogAction BehaviorInfoDialog::Draw() {
    if (m_Open) {
        m_Open = false;
        ImGui::OpenPopup("Behavior Information###behaviorinfo");
    }
    if (!ImGui::BeginPopupModal("Behavior Information###behaviorinfo", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return DialogAction::None;
    const bool valid = DrawBehaviorInfoFields(m_Fields, false);
    ImGui::BeginDisabled(!valid);
    DialogAction action = DialogAction::None;
    if (ImGui::Button("OK", ImVec2(80, 0))) {
        if (m_Description.name.empty()) m_Description.name = Utf8ToAnsi(m_Fields.name);
        m_Description.author = Utf8ToAnsi(m_Fields.author);
        m_Description.description = Utf8ToAnsi(m_Fields.description);
        m_Description.version = static_cast<float>(std::atof(m_Fields.version.c_str()));
        m_Written = WriteBehaviorDescription(m_Path, m_Description);
        action = DialogAction::Ok;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) action = DialogAction::Cancel;
    if (action != DialogAction::None) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return action;
}

} // namespace vdev
