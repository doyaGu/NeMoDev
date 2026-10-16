#include "ui/controls/ValueEditor.h"
#include "ui/controls/TypeAhead.h"

#include <cfloat>
#include <cstring>
#include <vector>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include "core/graph/RuntimeGraph.h"
#include "core/runtime/TextEncoding.h"
#include "core/graph/ValueCommands.h"

namespace vdev {

std::string ParameterValueText(CKParameter *parameter) {
    return AnsiToUtf8(ParameterText(parameter));
}

void ParameterValueEditor(EditorState &state, ValueEditState &edit, CK_ID script, CKParameter *parameter,
                          bool editable) {
    if (!parameter) {
        ImGui::TextDisabled("(no value)");
        return;
    }
    if (!editable) {
        ImGui::TextWrapped("%s", ParameterValueText(parameter).c_str());
        return;
    }
    CKContext *context = state.document->Context();
    CKParameterManager *manager = context->GetParameterManager();
    const CK_ID id = parameter->GetID();
    const CKParameterType type = parameter->GetType();
    CKParameterTypeDesc *description = manager->GetParameterTypeDescription(type);
    const CKGUID guid = parameter->GetGUID();
    const std::vector<unsigned char> bytes = ParameterBytes(parameter);
    const bool word = bytes.size() == sizeof(int);
    int integer = 0;
    if (word) std::memcpy(&integer, bytes.data(), sizeof(int));
    auto report = [&](const EditResult &result) {
        if (!result.ok && !result.Unchanged())
            state.Log(LogLevel::Warning, "Set value: " + AnsiToUtf8(result.message));
    };
    auto commit = [&](const std::vector<unsigned char> &value) {
        report(SetParameterBytes(*state.document, script, id, value));
    };
    auto commitInt = [&](int value) {
        std::vector<unsigned char> raw(sizeof(int));
        std::memcpy(raw.data(), &value, sizeof(int));
        commit(raw);
    };
    // Drag and text widgets: load from CK unless being edited; commit once.
    auto track = [&](bool commitNow, const std::function<void()> &apply) {
        if (ImGui::IsItemActivated()) edit.editing = id;
        if (commitNow) {
            apply();
            edit.editing = 0;
        } else if (!ImGui::IsItemActive() && edit.editing == id) {
            edit.editing = 0;
        }
    };
    ImGui::PushID(static_cast<int>(id));
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool enums = description && (description->dwFlags & CKPARAMETERTYPE_ENUMS);
    const bool flags = description && (description->dwFlags & CKPARAMETERTYPE_FLAGS);
    if (guid == CKPGUID_BOOL && word) {
        bool value = integer != 0;
        if (ImGui::Checkbox("##value", &value)) commitInt(value ? TRUE : FALSE);
    } else if (enums && word && manager->GetEnumDescByType(type)) {
        CKEnumStruct *enumeration = manager->GetEnumDescByType(type);
        if (ImGui::BeginCombo("##value", ParameterValueText(parameter).c_str())) {
            TypeAhead typeAhead;
            for (int i = 0; i < enumeration->GetNumEnums(); ++i) {
                const char *text = enumeration->GetEnumDescription(i);
                const bool selected = enumeration->GetEnumValue(i) == integer;
                if (typeAhead.Selectable(text ? AnsiToUtf8(text).c_str() : "?", selected))
                    commitInt(enumeration->GetEnumValue(i));
            }
            ImGui::EndCombo();
        }
    } else if (flags && word && manager->GetFlagsDescByType(type)) {
        CKFlagsStruct *flagSet = manager->GetFlagsDescByType(type);
        for (int i = 0; i < flagSet->GetNumFlags(); ++i) {
            const int bit = flagSet->GetFlagValue(i);
            bool on = bit != 0 && (integer & bit) == bit;
            const char *text = flagSet->GetFlagDescription(i);
            if (ImGui::Checkbox(text ? AnsiToUtf8(text).c_str() : "?", &on))
                commitInt(on ? (integer | bit) : (integer & ~bit));
        }
    } else if (guid == CKPGUID_INT && word) {
        if (edit.editing != id) edit.integer = integer;
        ImGui::DragInt("##value", &edit.integer);
        track(ImGui::IsItemDeactivatedAfterEdit(), [&] { commitInt(edit.integer); });
    } else if (guid == CKPGUID_FLOAT && bytes.size() == sizeof(float)) {
        float current = 0.0f;
        std::memcpy(&current, bytes.data(), sizeof(float));
        if (edit.editing != id) edit.real = current;
        ImGui::DragFloat("##value", &edit.real, 0.01f, 0.0f, 0.0f, "%.4g");
        track(ImGui::IsItemDeactivatedAfterEdit(), [&] {
            std::vector<unsigned char> raw(sizeof(float));
            std::memcpy(raw.data(), &edit.real, sizeof(float));
            commit(raw);
        });
    } else {
        // Any other type: the type's own string conversion (Dev's text form).
        if (edit.editing != id) edit.text = ParameterValueText(parameter);
        const bool entered = ImGui::InputText("##value", &edit.text, ImGuiInputTextFlags_EnterReturnsTrue);
        track(entered || ImGui::IsItemDeactivatedAfterEdit(), [&] {
            report(SetParameterText(*state.document, script, id, Utf8ToAnsi(edit.text)));
        });
    }
    ImGui::PopID();
}

bool DependenciesEditor(CKDependencies &dependencies, CK_DEPENDENCIES_OPMODE mode, float height) {
    bool changed = false;
    static const char *const modes[] = {"Custom", "None", "Full"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::RadioButton(modes[i], static_cast<int>(dependencies.m_Flags) == i)) {
            dependencies.m_Flags = static_cast<CK_DEPENDENCIES_FLAGS>(i);
            changed = true;
        }
    }
    const int classes = CKGetClassCount();
    if (dependencies.Size() < classes) {
        const int old = dependencies.Size();
        dependencies.Resize(classes);
        for (int i = old; i < classes; ++i) dependencies[i] = 0;
    }
    ImGui::BeginDisabled(dependencies.m_Flags != CK_DEPENDENCIES_CUSTOM);
    if (ImGui::BeginTable("##dependencies", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int cid = 0; cid < classes; ++cid) {
            CKClassDesc *desc = CKGetClassDesc(cid);
            if (!desc || !desc->DependsFct || !desc->DependsCountFct) continue;
            const int count = desc->DependsCountFct(mode);
            if (count <= 0) continue;
            ImGui::PushID(static_cast<int>(cid));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const char *name = CKClassIDToString(cid);
            ImGui::TextUnformatted(name ? name : "?");
            ImGui::TableSetColumnIndex(1);
            CKDWORD &bits = dependencies[cid];
            for (int k = 0; k < count && k < 32; ++k) {
                const char *option = desc->DependsFct(k, mode);
                if (!option) continue;
                const float width = ImGui::CalcTextSize(option).x + ImGui::GetFrameHeight() + 12.0f;
                if (k && ImGui::GetContentRegionAvail().x > width) ImGui::SameLine();
                bool on = (bits & (1u << k)) != 0;
                ImGui::PushID(k);
                if (ImGui::Checkbox(option, &on)) {
                    bits = on ? (bits | (1u << k)) : (bits & ~(1u << k));
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    return changed;
}

} // namespace vdev
