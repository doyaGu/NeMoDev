#include "ui/controls/BehaviorInfoFields.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

namespace vdev {

bool DrawBehaviorInfoFields(BehaviorInfoFields &fields, bool hideRepresentation) {
    ImGui::Text("Behavior :  %s", fields.name.c_str());
    ImGui::Text("Applies to Object of Type :  %s", fields.type.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Author :");
    ImGui::SameLine(90);
    ImGui::SetNextItemWidth(190);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##author", &fields.author);
    ImGui::SameLine();
    ImGui::TextUnformatted("Version :");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::InputText("##version", &fields.version, ImGuiInputTextFlags_CharsScientific);
    ImGui::TextUnformatted("Description");
    ImGui::InputTextMultiline("##description", &fields.description, ImVec2(510, 120));
    if (hideRepresentation)
        ImGui::Checkbox("Hide script representation in schematic view", &fields.hide);
    char *end = nullptr;
    const float version = std::strtof(fields.version.c_str(), &end);
    return fields.version.empty() || (std::isfinite(version) && end == fields.version.c_str() + fields.version.size());
}

} // namespace vdev
