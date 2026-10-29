#include "ui/controls/BehaviorInfoDialog.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include "core/runtime/TextEncoding.h"

namespace vdev {

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
