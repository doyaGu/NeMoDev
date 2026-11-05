#include "ui/controls/BehaviorInfoFields.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "imgui.h"
#include "ui/setup/SetupLayout.h"
#include "misc/cpp/imgui_stdlib.h"

namespace vdev {

bool DrawBehaviorInfoFields(BehaviorInfoFields &fields, bool hideRepresentation) {
    const float s = setup::PageUnit();
    ImGui::TextWrapped("Behavior :  %s", fields.name.c_str());
    ImGui::TextWrapped("Applies to Object of Type :  %s", fields.type.c_str());
    {
        setup::SetupForm form("##behavior-info");
        if (form) {
            form.Row("Author :");
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::InputText("##author", &fields.author);
            form.Row("Version :");
            ImGui::InputText("##version", &fields.version, ImGuiInputTextFlags_CharsScientific);
        }
    }
    ImGui::TextUnformatted("Description");
    ImGui::InputTextMultiline("##description", &fields.description, {-1, 120 * s});
    if (hideRepresentation)
        ImGui::Checkbox("Hide script representation in schematic view", &fields.hide);
    char *end = nullptr;
    const float version = std::strtof(fields.version.c_str(), &end);
    return fields.version.empty() || (std::isfinite(version) && end == fields.version.c_str() + fields.version.size());
}

} // namespace vdev
