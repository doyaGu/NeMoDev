#include "features/scene/SceneViews.h"
#include "features/scene/SceneViewData.h"
#include "editor/ImGuiDrawing.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "ui/controls/DevTheme.h"
#include "ui/controls/TypeAhead.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_internal.h"
#include <algorithm>
#include <set>

namespace vdev {
namespace {
class AttributeView {
  public:
    explicit AttributeView(bool chooser) : chooser(chooser) {}
    void Draw(const EditorHost *, EditorView, EditorViewFrame &);
    EditorStatus Navigate(const EditorRequest &);
    EditorStatus Inspect(EditorSink) const;
    EditorStatus Save(EditorSink) const;
    EditorStatus Restore(const EditorRequest &);
    uint64_t revision = 1;

  private:
    bool chooser, more = false, openDialog = false, dialog = false;
    DocumentToken document{};
    SceneAttributeCatalog catalog;
    uint64_t modelRevision = 0;
    std::set<uint64_t> chosen;
    int64_t selected = -1;
    std::set<ObjectRef> selectedRows;
    std::vector<ObjectRef> targets;
    std::vector<SceneAttributeRow> rows;
    std::set<uint64_t> rowsTypes;
    std::vector<std::pair<uint64_t, std::string>> parameterTypes;
    uint64_t parameter = 0;
    std::string categoryName, typeName, typeCategory, renameText, renameCategory, deleteCategory, valueText;
    int64_t renameType = -1;
    bool renameFocus = false, removeUser = false;
    EditorEdit valueEdit = 0;
    ObjectRef valueObject{};
    uint64_t valueType = 0;
    float split = 0;
    EditorOperation createdType = 0, createdCategory = 0, added = 0;
    void Read(const EditorClient &);
    void TypeTree(const EditorClient &, bool multiple);
    void CreatePanel(const EditorClient &);
    void Chooser(const EditorClient &, EditorViewFrame &);
    void Manager(const EditorClient &, EditorViewFrame &);
    void Values(const EditorClient &, float width, float height);
    void Command(const EditorClient &client, const char *id, std::vector<EditorValue> args = {}) {
        args.push_back(IntegerArgument("catalog", catalog.version));
        client.Command(id, args);
    }
};
void AttributeView::Read(const EditorClient &client) {
    const auto next = client.Scope().document;
    if (document != next) {
        document = next;
        catalog = {};
        modelRevision = 0;
        chosen.clear();
        selected = -1;
        selectedRows.clear();
        rows.clear();
        client.CancelEdit(valueEdit);
        valueObject = {};
        renameType = -1;
        renameCategory.clear();
        deleteCategory.clear();
        targets.clear();
        dialog = openDialog = false;
        createdType = createdCategory = added = 0;
        ++revision;
    }
    int64_t acceptedType = -1;
    if (createdType) {
        const auto result = client.Completion(createdType);
        if (result.status != EDITOR_QUEUED) {
            if (result.status == EDITOR_OK) {
                const auto id = EditorClient::Integer(result, "type");
                acceptedType = int64_t(id);
                typeName.clear();
                ++revision;
            }
            createdType = 0;
        }
    }
    if (createdCategory) {
        const auto result = client.Completion(createdCategory);
        if (result.status != EDITOR_QUEUED) {
            if (result.status == EDITOR_OK) {
                typeCategory = categoryName;
                categoryName.clear();
            }
            createdCategory = 0;
        }
    }
    if (added) {
        const auto result = client.Completion(added);
        if (result.status != EDITOR_QUEUED) {
            if (result.status == EDITOR_OK) {
                dialog = false;
            }
            added = 0;
        }
    }
    const auto version = client.Revision("attribute.catalog");
    if (version == modelRevision) {
        return;
    }
    auto read = QuerySceneAttributes(client, chooser ? targets : std::vector<ObjectRef>{});
    if (!read.available) {
        catalog = std::move(read);
        rows.clear();
        return;
    }
    const bool definitionsChanged = catalog.version && catalog.version != read.version;
    catalog = std::move(read);
    modelRevision = version;
    // Native type indices can be reused. A changed catalog invalidates choices
    // instead of applying a pending operation to a newly created definition.
    if (definitionsChanged) {
        chosen.clear();
        selected = -1;
        renameType = -1;
        renameCategory.clear();
        deleteCategory.clear();
        valueObject = {};
        selectedRows.clear();
    }
    if (acceptedType >= 0 && catalog.Find(uint64_t(acceptedType))) {
        chosen = {uint64_t(acceptedType)};
        selected = acceptedType;
    }
    if (selected >= 0 && !catalog.Find(uint64_t(selected))) {
        selected = -1;
    }
    for (auto it = chosen.begin(); it != chosen.end();) {
        if (!catalog.Find(*it)) {
            it = chosen.erase(it);
        } else {
            ++it;
        }
    }
    const auto parameters = client.Query("scene.parameter-types");
    const auto guids = SceneIntegers(parameters, "guids");
    const auto names = SceneTexts(parameters, "names");
    parameterTypes.clear();
    if (guids.size() == names.size()) {
        for (size_t i = 0; i < guids.size(); ++i) {
            parameterTypes.push_back({guids[i], names[i]});
        }
    }
    rowsTypes.clear();
    ++revision;
}
void AttributeView::TypeTree(const EditorClient &client, bool multiple) {
    for (const auto &category : catalog.categories) {
        if (category.types.empty() && chooser && !targets.empty()) {
            continue;
        }
        const auto label = category.name.empty() ? std::string("(No category)") : category.name;
        if (!chooser) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
        }
        const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        if (!chooser && !category.name.empty() && (category.flags & 3) &&
            ImGui::BeginPopupContextItem("categorymenu")) {
            if ((category.flags & 1) && ImGui::MenuItem("Rename")) {
                renameCategory = category.name;
                renameText = category.name;
                renameFocus = true;
            }
            if ((category.flags & 2) && ImGui::MenuItem("Delete")) {
                deleteCategory = category.name;
                ImGui::OpenPopup("Delete Attribute Category");
            }
            ImGui::EndPopup();
        }
        if (!open) {
            continue;
        }
        for (auto id : category.types) {
            const auto *type = catalog.Find(id);
            if (!type) {
                continue;
            }
            ImGui::PushID(int(id));
            if (!chooser) {
                ImGui::TableNextRow();
            }
            if (!chooser) {
                ImGui::TableSetColumnIndex(0);
            }
            if (renameType == int64_t(id)) {
                if (renameFocus) {
                    ImGui::SetKeyboardFocusHere();
                    renameFocus = false;
                }
                const bool entered = ImGui::InputText(
                    "##rename", &renameText, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if (entered || ImGui::IsItemDeactivatedAfterEdit()) {
                    Command(client, "attribute.rename-type",
                            {IntegerArgument("type", id), TextArgument("name", renameText)});
                    renameType = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    renameType = -1;
                }
            } else {
                if (ImGui::Selectable(type->name.c_str(), chosen.count(id) != 0,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (!multiple || !ImGui::GetIO().KeyCtrl) {
                        chosen.clear();
                    }
                    if (multiple && ImGui::GetIO().KeyCtrl && chosen.count(id)) {
                        chosen.erase(id);
                    } else {
                        chosen.insert(id);
                    }
                    selected = int64_t(id);
                    selectedRows.clear();
                    valueObject = {};
                    rowsTypes.clear();
                    ++revision;
                }
                if (ImGui::BeginDragDropSource()) {
                    const EditorAttributePayload payload{int32_t(id), {}, catalog.version};
                    ImGui::SetDragDropPayload("SCHEMATIC_ATTRIBUTE", &payload, sizeof(payload), ImGuiCond_Once);
                    ImGui::TextUnformatted(type->name.c_str());
                    ImGui::EndDragDropSource();
                }
                if ((type->flags & 3) && ImGui::BeginPopupContextItem("typemenu")) {
                    if ((type->flags & 1) && ImGui::MenuItem("Rename")) {
                        renameType = int64_t(id);
                        renameText = type->name;
                        renameFocus = true;
                    }
                    if ((type->flags & 2) && ImGui::MenuItem("Delete")) {
                        Command(client, "attribute.delete-type", {IntegerArgument("type", id)});
                    }
                    ImGui::EndPopup();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Type: %s\nApply to: %s",
                                  type->parameterName.empty() ? "None" : type->parameterName.c_str(),
                                  type->compatibleName.c_str());
            }
            if (!chooser) {
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(type->parameterName.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(type->compatibleName.c_str());
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}
void AttributeView::CreatePanel(const EditorClient &client) {
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Create New Category");
    ImGui::Separator();
    ImGui::InputText("##categoryname", &categoryName);
    if (ImGui::Button("Create##category", ImVec2(80, 0))) {
        const auto result = client.Command("attribute.create-category", {TextArgument("name", categoryName)});
        createdCategory = result.operation;
    }
    ImGui::Dummy(ImVec2(1, 10));
    ImGui::TextUnformatted("Create New Attribute Type");
    ImGui::Separator();
    ImGui::TextUnformatted("Name");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(240);
    ImGui::InputText("##typename", &typeName);
    ImGui::TextUnformatted("Parameter Type");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(240);
    std::string shown = "None";
    for (const auto &type : parameterTypes) {
        if (type.first == parameter) {
            shown = type.second;
        }
    }
    if (ImGui::BeginCombo("##typeparameter", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        TypeAhead typeAhead;
        if (typeAhead.Selectable("None", parameter == 0)) {
            parameter = 0;
        }
        for (const auto &type : parameterTypes) {
            if (typeAhead.Selectable(type.second.c_str(), type.first == parameter)) {
                parameter = type.first;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextUnformatted("Category");
    ImGui::SameLine(130);
    ImGui::SetNextItemWidth(240);
    if (ImGui::BeginCombo("##typecategory", typeCategory.empty() ? "(No category)" : typeCategory.c_str())) {
        TypeAhead typeAhead;
        if (typeAhead.Selectable("(No category)", typeCategory.empty())) {
            typeCategory.clear();
        }
        for (const auto &category : catalog.categories) {
            if (!category.name.empty() && typeAhead.Selectable(category.name.c_str(), typeCategory == category.name)) {
                typeCategory = category.name;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Create##type", ImVec2(80, 0))) {
        const auto result = client.Command("attribute.create-type",
                                           {TextArgument("name", typeName), IntegerArgument("parameter", parameter),
                                            TextArgument("category", typeCategory)});
        createdType = result.operation;
    }
    ImGui::EndGroup();
}
void AttributeView::Chooser(const EditorClient &client, EditorViewFrame &frame) {
    if (openDialog) {
        ImGui::OpenPopup("Add Attribute");
        openDialog = false;
        dialog = true;
    }
    if (!dialog) {
        if (ImGui::BeginPopupModal("Add Attribute", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        return;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    if (!ImGui::BeginPopupModal("Add Attribute", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::BeginGroup();
    if (ImGui::BeginChild("tree", ImVec2(300, 360), ImGuiChildFlags_Borders)) {
        TypeTree(client, true);
    }
    ImGui::EndChild();
    ImGui::BeginDisabled(targets.empty() || chosen.empty() || added);
    if (ImGui::Button("Add Selected", ImVec2(120, 0))) {
        const std::vector<uint64_t> types(chosen.begin(), chosen.end());
        const auto result = client.Command(
            "attribute.add-selected",
            {ObjectsArgument("objects", targets),
             IntegerArgument("catalog", catalog.version),
             {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {types.data(), uint32_t(types.size())}}});
        added = result.operation;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        dialog = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(more ? "<< Less" : "More >>", ImVec2(80, 0))) {
        more = !more;
    }
    ImGui::EndGroup();
    if (more) {
        ImGui::SameLine();
        CreatePanel(client);
    }
    frame.active = dialog;
    ImGui::EndPopup();
}
void AttributeView::Values(const EditorClient &client, float width, float height) {
    if (rowsTypes != chosen) {
        rows = QueryAttributeObjects(client, catalog, chosen);
        rowsTypes = chosen;
        std::set<ObjectRef> live;
        for (const auto &row : rows) {
            live.insert(row.object);
        }
        for (auto it = selectedRows.begin(); it != selectedRows.end();) {
            if (!live.count(*it)) {
                it = selectedRows.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (!ImGui::BeginTable("attributeobjects", 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_ScrollY,
                           ImVec2(width, height))) {
        return;
    }
    ImGui::TableSetupColumn("Objects");
    ImGui::TableSetupColumn("Attribute");
    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (const auto &row : rows) {
        ImGui::PushID(int(row.type));
        ImGui::PushID(int(row.object.object));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool chosenRow = selectedRows.count(row.object) != 0;
        if (ImGui::Selectable((row.name + "###row").c_str(), chosenRow, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (!ImGui::GetIO().KeyCtrl) {
                selectedRows = {row.object};
            } else if (chosenRow) {
                selectedRows.erase(row.object);
            } else {
                selectedRows.insert(row.object);
            }
            ++revision;
            if (ImGui::IsMouseDoubleClicked(0)) {
                client.Command("object.open-setup", {ObjectArgument("target", row.object)});
            }
        }
        if (ImGui::BeginDragDropSource()) {
            const EditorAttributePayload payload{int32_t(row.type), row.parameter, catalog.version};
            ImGui::SetDragDropPayload("SCHEMATIC_ATTRIBUTE", &payload, sizeof(payload), ImGuiCond_Once);
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("objectmenu")) {
            const std::vector<ObjectRef> targets =
                selectedRows.count(row.object) ? std::vector<ObjectRef>(selectedRows.begin(), selectedRows.end())
                                               : std::vector<ObjectRef>{row.object};
            if (ImGui::MenuItem("Copy Attribute Parameter Value", "Ctrl+C")) {
                Command(client, "attribute.copy",
                        {ObjectsArgument("objects", targets), IntegerArgument("type", row.type)});
            }
            if (ImGui::MenuItem("Paste Attribute Parameter Value", "Ctrl+V")) {
                Command(client, "attribute.paste",
                        {ObjectsArgument("objects", targets), IntegerArgument("type", row.type)});
            }
            if (ImGui::MenuItem("Show Setup")) {
                client.Command("object.open-setup", {ObjectArgument("target", row.object)});
            }
            if (ImGui::MenuItem("Show Object In Level Manager")) {
                client.Command("scene.show-object", {ObjectArgument("target", row.object)});
            }
            if (ImGui::MenuItem("Remove Attribute from Objects")) {
                const std::vector<uint64_t> types(chosen.begin(), chosen.end());
                Command(client, "attribute.remove",
                        {ObjectsArgument("objects", targets),
                         {EDITOR_TEXT("types"),
                          EDITOR_INTEGER_LIST,
                          {},
                          {},
                          0,
                          {},
                          {types.data(), uint32_t(types.size())}}});
            }
            ImGui::EndPopup();
        }
        ImGui::TableSetColumnIndex(1);
        if (const auto *type = catalog.Find(row.type)) {
            ImGui::TextUnformatted(type->name.c_str());
        }
        ImGui::TableSetColumnIndex(2);
        if (row.parameter) {
            const auto description = client.Query("parameter.description", {ObjectArgument("target", row.parameter)});
            const auto property = EditorClient::Text(description, "property");
            if (!property.empty()) {
                ImGui::SetNextItemWidth(-1);
                const auto action = client.DrawProperty(property, row.parameter, valueEdit, "##value");
                if (action & EDITOR_INPUT_ACCEPT) rowsTypes.clear();
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    client.CancelEdit(valueEdit);
                    ImGui::ClearActiveID();
                    client.Command("parameter.edit", {ObjectArgument("target", row.parameter)});
                }
            } else {
            std::string text = valueObject == row.object && valueType == row.type ? valueText : row.value;
            ImGui::SetNextItemWidth(-1);
            const bool accept = ImGui::InputText("##value", &text, ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemEdited()) {
                valueObject = row.object;
                valueType = row.type;
                valueText = text;
            }
            if ((accept || ImGui::IsItemDeactivatedAfterEdit()) && valueObject == row.object && valueType == row.type) {
                Command(client, "attribute.set-text",
                        {ObjectArgument("target", row.object), IntegerArgument("type", row.type),
                         TextArgument("text", valueText)});
                valueObject = {};
                rowsTypes.clear();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                ImGui::ClearActiveID();
                valueObject = {};
                client.Command("parameter.edit", {ObjectArgument("target", row.parameter)});
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                valueObject = {};
            }
            }
        }
        ImGui::PopID();
        ImGui::PopID();
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_A)) {
        selectedRows.clear();
        for (const auto &row : rows) {
            selectedRows.insert(row.object);
        }
        ++revision;
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        const std::vector<ObjectRef> objects(selectedRows.begin(), selectedRows.end());
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !objects.empty()) {
            const std::vector<uint64_t> types(chosen.begin(), chosen.end());
            Command(
                client, "attribute.remove",
                {ObjectsArgument("objects", objects),
                 {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {types.data(), uint32_t(types.size())}}});
        }
        if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && !objects.empty()) {
            client.Command("object.open-setup", {ObjectArgument("target", objects.front())});
        }
    }
    ImGui::EndTable();
}
void AttributeView::Manager(const EditorClient &client, EditorViewFrame &frame) {
    EditorPrepareViewWindow(frame);
    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = BeginDevWindow(frame.window.data, frame.closable ? &open : nullptr);
    ImGui::PopStyleVar();
    if (visible) {
        if (frame.closable) {
            DevTabClose(&open);
        }
        const auto origin = ImGui::GetCursorScreenPos(), room = ImGui::GetContentRegionAvail();
        const float s = ImGui::GetFontSize() / 13;
        auto at = [&](float x, float y) { return ImVec2(origin.x + x * s, origin.y + y * s); };
        auto *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(origin.x + room.x, origin.y + 21 * s), vi::Color(2));
        const std::vector<ObjectRef> objects(selectedRows.begin(), selectedRows.end());
        auto copy = [&] {
            if (selected >= 0) {
                Command(client, "attribute.copy",
                        {ObjectsArgument("objects", objects), IntegerArgument("type", uint64_t(selected))});
            }
        };
        auto paste = [&] {
            if (selected >= 0) {
                Command(client, "attribute.paste",
                        {ObjectsArgument("objects", objects), IntegerArgument("type", uint64_t(selected))});
            }
        };
        static const int COPY[5] = {1057, 1058, 1059, 1060, 1057}, PASTE[5] = {1061, 1062, 1063, 1064, 1061};
        if (vi::ToolbarButton("create", at(9, 3), at(95, 18), "Create Attribute", s)) {
            client.Command("attribute.choose", {IntegerArgument("more", 1)});
        }
        if (DevBitmapButton("copy", at(100, 1), COPY, false, true, s, "Copy Selected Item Value")) {
            copy();
        }
        if (DevBitmapButton("paste", at(121, 1), PASTE, false, true, s, "Paste Value On Selected Items")) {
            paste();
        }
        if (vi::ToolbarButton("removeuser", at(141, 3), at(271, 18), "Remove All User Attributes", s)) {
            removeUser = true;
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
                copy();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
                paste();
            }
        }
        const float top = origin.y + 21 * s, left = origin.x + 6 * s, height = std::max(1.0f, room.y - 21 * s),
                    available = room.x - 6 * s;
        if (split <= 0) {
            split = available * .5f;
        }
        split = std::clamp(split, 40 * s, std::max(40 * s, available - 40 * s));
        ImGui::SetCursorScreenPos(ImVec2(left + split, top));
        ImGui::InvisibleButton("##split", ImVec2(2 * s, height));
        if (ImGui::IsItemActive()) {
            split += ImGui::GetIO().MouseDelta.x;
        }
        if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        ImGui::SetCursorScreenPos(ImVec2(left, top));
        if (ImGui::BeginTable("attributetypes", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX,
                              ImVec2(split, height))) {
            ImGui::TableSetupColumn("Attributes", ImGuiTableColumnFlags_WidthFixed, 250 * s);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100 * s);
            ImGui::TableSetupColumn("Apply to", ImGuiTableColumnFlags_WidthFixed, 100 * s);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            // Each category and its types occupy their own table rows.
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            TypeTree(client, true);
            ImGui::EndTable();
        }
        ImGui::SetCursorScreenPos(ImVec2(left + split + 2 * s, top));
        Values(client, std::max(1.0f, available - split - 2 * s), height);
        if (!renameCategory.empty()) {
            ImGui::OpenPopup("Rename Attribute Category");
        }
        if (ImGui::BeginPopupModal("Rename Attribute Category", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (renameFocus) {
                ImGui::SetKeyboardFocusHere();
                renameFocus = false;
            }
            const bool accept = ImGui::InputText("##rename", &renameText, ImGuiInputTextFlags_EnterReturnsTrue);
            if (accept || ImGui::Button("OK")) {
                Command(client, "attribute.rename-category",
                        {TextArgument("category", renameCategory), TextArgument("name", renameText)});
                renameCategory.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                renameCategory.clear();
                ImGui::CloseCurrentPopup();
            }
            frame.active = 1;
            ImGui::EndPopup();
        }
        if (!deleteCategory.empty()) {
            ImGui::OpenPopup("Delete Attribute Category");
        }
        if (ImGui::BeginPopupModal("Delete Attribute Category", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Delete this category and its attribute types?\nUndo stack will be cleared.");
            if (ImGui::Button("OK")) {
                Command(client, "attribute.delete-category", {TextArgument("category", deleteCategory)});
                deleteCategory.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                deleteCategory.clear();
                ImGui::CloseCurrentPopup();
            }
            frame.active = 1;
            ImGui::EndPopup();
        }
        if (removeUser) {
            ImGui::OpenPopup("Remove All User Attributes");
        }
        if (ImGui::BeginPopupModal("Remove All User Attributes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                "Are you sure you want to remove all user attributes ?\nUndo stack will be cleared.");
            if (ImGui::Button("OK")) {
                Command(client, "attribute.remove-user");
                removeUser = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                removeUser = false;
                ImGui::CloseCurrentPopup();
            }
            frame.active = 1;
            ImGui::EndPopup();
        }
        if (valueEdit || valueObject || renameType >= 0) {
            frame.active = 1;
        }
    }
    ImGui::End();
    if (!open) {
        frame.close = 1;
    }
}
void AttributeView::Draw(const EditorHost *host, EditorView id, EditorViewFrame &frame) {
    EditorClient client(host, id);
    Read(client);
    if (chooser) {
        Chooser(client, frame);
    } else {
        Manager(client, frame);
    }
}
EditorStatus AttributeView::Navigate(const EditorRequest &request) {
    std::vector<ObjectRef> next;
    bool nextMore = false;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &field = request.values[i];
        const std::string key(field.key.data, field.key.size);
        if (key == "objects" && field.objects.count) {
            next.assign(field.objects.data, field.objects.data + field.objects.count);
        }
        if (key == "more") {
            nextMore = field.integer != 0;
        }
    }
    document = request.scope.document;
    targets = std::move(next);
    more = nextMore;
    openDialog = chooser;
    chosen.clear();
    modelRevision = 0;
    ++revision;
    return EDITOR_OK;
}
EditorStatus AttributeView::Inspect(EditorSink sink) const {
    const std::vector<ObjectRef> objects(selectedRows.begin(), selectedRows.end());
    const std::vector<uint64_t> types(chosen.begin(), chosen.end());
    const EditorValue fields[] = {
        {EDITOR_TEXT("selection"), EDITOR_OBJECT_LIST, {}, {}, 0, {objects.data(), uint32_t(objects.size())}},
        {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {types.data(), uint32_t(types.size())}},
        {EDITOR_TEXT("catalog"), EDITOR_INTEGER_VALUE, {}, {}, catalog.version}};
    return sink.write ? sink.write(sink.data, fields, 3) : EDITOR_INVALID;
}
EditorStatus AttributeView::Save(EditorSink sink) const {
    const EditorValue fields[] = {{EDITOR_TEXT("split"), EDITOR_REAL_VALUE, {}, {}, 0, {}, {}, {}, double(split)},
                                  {EDITOR_TEXT("more"), EDITOR_INTEGER_VALUE, {}, {}, more}};
    return sink.write ? sink.write(sink.data, fields, 2) : EDITOR_INVALID;
}
EditorStatus AttributeView::Restore(const EditorRequest &request) {
    if (request.count != 2) {
        return EDITOR_INVALID;
    }
    float next = 0;
    bool nextMore = false;
    for (uint32_t i = 0; i < request.count; ++i) {
        const auto &field = request.values[i];
        const std::string key(field.key.data, field.key.size);
        if (key == "split" && field.type == EDITOR_REAL_VALUE && field.real >= 0 && field.real <= 100000) {
            next = float(field.real);
        } else if (key == "more" && field.type == EDITOR_INTEGER_VALUE && field.integer <= 1) {
            nextMore = field.integer != 0;
        } else {
            return EDITOR_INVALID;
        }
    }
    split = next;
    more = nextMore;
    return EDITOR_OK;
}
EditorStatus Choose(void *, const EditorHost *host, const EditorRequest *request, EditorSink sink) {
    return host->navigateView(host->data, EditorString EDITOR_TEXT("attribute.chooser"), 0, request, sink);
}
} // namespace
bool RegisterAttributeViews(EditorApplication &editor, std::string &error) {
    static int chooser;
    EditorViewDesc views[2]{};
    const EditorParameter navigate[] = {{EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST, 1},
                                        {EDITOR_TEXT("more"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("selection"), EDITOR_OBJECT_LIST},
                                      {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST},
                                      {EDITOR_TEXT("catalog"), EDITOR_INTEGER_VALUE}};
    for (int i = 0; i < 2; ++i) {
        auto &view = views[i];
        view.size = sizeof(view);
        view.imguiVersion = EDITOR_IMGUI_VERSION;
        view.id = i ? EditorString EDITOR_TEXT("attribute.chooser") : EditorString EDITOR_TEXT("scene.attributes");
        view.provider = EDITOR_TEXT("builtin.scene");
        view.title = i ? EditorString EDITOR_TEXT("Add Attribute") : EditorString EDITOR_TEXT("Attribute Manager");
        view.kind = i ? EDITOR_OVERLAY_VIEW : EDITOR_WINDOW_VIEW;
        view.singleInstance = i;
        view.area = EDITOR_WORKSPACE;
        view.data = i ? &chooser : nullptr;
        view.create = [](void *data, const EditorHost *, EditorView) -> void * {
            return new AttributeView(data != nullptr);
        };
        view.destroy = [](void *data) { delete static_cast<AttributeView *>(data); };
        view.draw = [](void *data, const EditorHost *host, EditorView id, EditorViewFrame *frame) {
            static_cast<AttributeView *>(data)->Draw(host, id, *frame);
        };
        view.parameters = navigate;
        view.count = 2;
        view.navigate = [](void *data, const EditorHost *, const EditorRequest *request) {
            return static_cast<AttributeView *>(data)->Navigate(*request);
        };
        view.stateVersion = 1;
        static const EditorParameter settings[] = {{EDITOR_TEXT("split"), EDITOR_REAL_VALUE},
                                                   {EDITOR_TEXT("more"), EDITOR_INTEGER_VALUE}};
        if (view.kind == EDITOR_WINDOW_VIEW) {
            view.settings = settings;
            view.settingCount = uint32_t(std::size(settings));
        }
        view.saveState = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            return static_cast<AttributeView *>(data)->Save(sink);
        };
        view.restoreState = [](void *data, const EditorHost *, const EditorRequest *request) {
            return static_cast<AttributeView *>(data)->Restore(*request);
        };
        view.inspect = [](void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
            return static_cast<AttributeView *>(data)->Inspect(sink);
        };
        view.inspectRevision = [](void *data, const EditorHost *) {
            return static_cast<AttributeView *>(data)->revision;
        };
        view.fields = fields;
        view.fieldCount = 3;
    }
    const EditorCommandDesc choose{sizeof(EditorCommandDesc),
                                   EDITOR_TEXT("attribute.choose"),
                                   EDITOR_TEXT("builtin.scene"),
                                   EDITOR_TEXT("Add Attribute..."),
                                   navigate,
                                   2,
                                   Choose,
                                   nullptr,
                                   EDITOR_DOCUMENT,
                                   EDITOR_SESSION_CHANGE};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("scene.attribute-views");
    module.views = views;
    module.viewCount = 2;
    module.commands = &choose;
    module.commandCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
