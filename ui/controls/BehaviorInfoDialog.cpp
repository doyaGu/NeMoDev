#include "ui/controls/BehaviorInfoDialog.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include "core/runtime/TextEncoding.h"

namespace vdev {

void BehaviorInfoDialog::Open(const std::string &nms, const std::string &name, CK_CLASSID cid, bool saving) {
    m_Path = nms;
    m_Saving = saving;
    m_Description = BehaviorDescription();
    // The .dsc there, then the behavior's name and class (sub_581FB0).
    const bool read = ReadBehaviorDescription(nms, m_Description);
    if (!name.empty()) m_Description.name = name;
    m_Name = !name.empty() || read ? AnsiToUtf8(m_Description.name) : std::filesystem::u8path(nms).stem().u8string();
    if (cid) m_Description.cid = cid;
    m_Author = AnsiToUtf8(m_Description.author);
    m_Text = AnsiToUtf8(m_Description.description);
    m_Version.clear();
    if (read) {
        char text[64];
        std::snprintf(text, sizeof(text), "%g", m_Description.version);
        m_Version = text;
    }
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
    const char *className = m_Description.cid ? CKClassIDToString(m_Description.cid) : nullptr;
    ImGui::Text("Behavior :  %s", m_Name.c_str());
    ImGui::Text("Applies to Object of Type :  %s", className ? AnsiToUtf8(className).c_str() : "");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Author :");
    ImGui::SameLine(90);
    ImGui::SetNextItemWidth(190);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##author", &m_Author);
    ImGui::SameLine();
    ImGui::TextUnformatted("Version :");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::InputText("##version", &m_Version, ImGuiInputTextFlags_CharsScientific);
    ImGui::TextUnformatted("Description");
    ImGui::InputTextMultiline("##description", &m_Text, ImVec2(510, 120));
    if (m_Saving) ImGui::Checkbox("Hide script representation in schematic view", &m_Hide);
    DialogAction action = DialogAction::None;
    if (ImGui::Button("OK", ImVec2(80, 0))) {
        if (m_Description.name.empty()) m_Description.name = Utf8ToAnsi(m_Name);
        m_Description.author = Utf8ToAnsi(m_Author);
        m_Description.description = Utf8ToAnsi(m_Text);
        m_Description.version = static_cast<float>(std::atof(m_Version.c_str()));
        m_Written = WriteBehaviorDescription(m_Path, m_Description);
        action = DialogAction::Ok;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) action = DialogAction::Cancel;
    if (action != DialogAction::None) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return action;
}

} // namespace vdev
