// "create": objects made in the editor on the sample opened from the command
// line, and their Setups: a 3D Frame (the 3D Layout's 3, its representation
// from its Setup and from the 3D Layout's shortcut menu), a Grid (5, its
// Priority), an Array (the Level Manager's R: a column, a row, a cell) and a
// Group (the Level Manager's G on a selected row). Each change undone, then
// the creation: the composition unmodified again.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <string>

#include "tests/ui/UiTests.h"

namespace vdevuitest {

namespace {

// The composition's objects of class cid (derived ones too), those undone
// (discarded) left out.
std::set<CK_ID> ObjectsOf(ImGuiTestContext *ctx, CK_CLASSID cid) {
    return Read<std::set<CK_ID>>(ctx, [cid] {
        std::set<CK_ID> ids;
        const XObjectPointerArray &list = App().State().host.Context()->GetObjectListByType(cid, TRUE);
        for (int i = 0; i < list.Size(); ++i)
            if (list[i] && !App().State().document->IsDiscarded(list[i]->GetID())) ids.insert(list[i]->GetID());
        return ids;
    });
}

// The one object of class cid not in before; 0 when not exactly one.
CK_ID MadeSince(ImGuiTestContext *ctx, CK_CLASSID cid, const std::set<CK_ID> &before) {
    CK_ID made = 0;
    int count = 0;
    for (CK_ID id : ObjectsOf(ctx, cid))
        if (!before.count(id)) {
            made = id;
            ++count;
        }
    return count == 1 ? made : 0;
}

// The 3D Layout focused with the mouse over its view, the key of a
// creation tool pressed (1 to 9): the new object of class cid.
CK_ID CreateInLayout(ImGuiTestContext *ctx, ImGuiKey key, CK_CLASSID cid) {
    Settle(ctx, "//3D Layout");
    ImVec2 min, max;
    if (!App().Layout().ShownView(min, max)) return 0;
    ctx->MouseMoveToPos(Center(min, max));
    const std::set<CK_ID> before = ObjectsOf(ctx, cid);
    ctx->KeyPress(key);
    ctx->Yield(3);
    const CK_ID made = MadeSince(ctx, cid, before);
    if (made) ctx->LogInfo("created '%s'", ObjectName(ctx, made).c_str());
    return made;
}

// A 3D Frame's representation (EditorState::frameRepresentations; a frame
// not listed has the first shape, 1).
int Representation(CK_ID frame) {
    const auto &representations = App().State().frameRepresentations;
    const auto found = representations.find(frame);
    return found == representations.end() ? 1 : found->second;
}

bool Discarded(ImGuiTestContext *ctx, CK_ID id) {
    return Read<bool>(ctx, [id] { return !Object<CKObject>(id) || App().State().document->IsDiscarded(id); });
}

} // namespace

void RegisterCreateTests(ImGuiTestEngine *engine) {
    ImGuiTest *t = nullptr;

    // 3 in the 3D Layout: a new 3D Frame, its Setup; the Representation
    // combo set to another shape; Ctrl+Z gives the first back, Ctrl+Z again
    // takes the frame out, unmodified.
    t = IM_REGISTER_TEST(engine, "create", "frame_setup_representation_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(!App().State().document->Dirty());
        const CK_ID frame = CreateInLayout(ctx, ImGuiKey_3, CKCID_3DENTITY);
        IM_CHECK_SILENT(frame != 0);
        IM_CHECK(App().State().document->Dirty());
        const char *ref = "//###setup3d";
        IM_CHECK_SILENT(ShowSetup(ctx, frame, ref));
        const std::vector<std::string> shapes = App().Layout().View().FrameShapeNames();
        IM_CHECK_SILENT(!shapes.empty());
        IM_CHECK_EQ(Representation(frame), 1);
        const bool second = shapes.size() > 1;
        ChooseInCombo(ctx, ref, "##representation", second ? shapes[1].c_str() : "No Representation");
        IM_CHECK_EQ(Representation(frame), second ? 2 : 0);
        Undo(ctx);
        IM_CHECK_EQ(Representation(frame), 1);
        Undo(ctx);
        IM_CHECK(Discarded(ctx, frame));
        IM_CHECK(!App().State().document->Dirty());
    };

    // The type-ahead in a combo's list: the Representation combo of a new
    // 3D Frame's Setup, "no representation" typed (it is before the shape
    // shown: the search wraps) and Escape: nothing chosen; typed again and
    // Enter: chosen; then a shape's first letter (the first shape starting
    // with it, after No Representation) and Enter; each choice one undo step.
    t = IM_REGISTER_TEST(engine, "create", "frame_setup_representation_type_ahead");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        IM_CHECK_SILENT(!App().State().document->Dirty());
        const CK_ID frame = CreateInLayout(ctx, ImGuiKey_3, CKCID_3DENTITY);
        IM_CHECK_SILENT(frame != 0);
        const char *ref = "//###setup3d";
        IM_CHECK_SILENT(ShowSetup(ctx, frame, ref));
        const std::vector<std::string> shapes = App().Layout().View().FrameShapeNames();
        IM_CHECK_SILENT(!shapes.empty() && !shapes.back().empty());
        IM_CHECK_EQ(Representation(frame), 1);
        TypeInCombo(ctx, ref, "##representation", "no representation", false);
        IM_CHECK_EQ(Representation(frame), 1);
        TypeInCombo(ctx, ref, "##representation", "no representation", true);
        IM_CHECK_EQ(Representation(frame), 0);
        const char letter[2] = {shapes.back()[0], 0};
        int first = 0;
        for (size_t i = 0; i < shapes.size() && !first; ++i)
            if (std::toupper(static_cast<unsigned char>(shapes[i][0])) == std::toupper(static_cast<unsigned char>(letter[0])))
                first = static_cast<int>(i) + 1;
        TypeInCombo(ctx, ref, "##representation", letter, true);
        IM_CHECK_EQ(Representation(frame), first);
        Undo(ctx);
        IM_CHECK_EQ(Representation(frame), 0);
        Undo(ctx);
        IM_CHECK_EQ(Representation(frame), 1);
        Undo(ctx);
        IM_CHECK(Discarded(ctx, frame));
        IM_CHECK(!App().State().document->Dirty());
    };

    // A new 3D Frame found in the view (its icon) and right-clicked, not
    // selected then selected: the shortcut menu's Change Representation > No
    // Representation changes it each time; Ctrl+Z gives it its shape back,
    // a last Ctrl+Z takes the frame out.
    t = IM_REGISTER_TEST(engine, "create", "layout_change_representation_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID frame = CreateInLayout(ctx, ImGuiKey_3, CKCID_3DENTITY);
        IM_CHECK_SILENT(frame != 0);
        ctx->WindowFocus("//3D Layout");
        ctx->Yield(3);
        // Made 10 units in front of the view's camera: in the middle of the
        // view. Its icon is small (the Cross): every pixel near the middle.
        const std::optional<ImVec2> at = Read<std::optional<ImVec2>>(ctx, [frame]() -> std::optional<ImVec2> {
            ImVec2 min, max;
            if (!App().Layout().ShownView(min, max)) return std::nullopt;
            const ImVec2 middle = Center(min, max);
            for (int radius = 0; radius <= 30; ++radius)
                for (int dy = -radius; dy <= radius; ++dy)
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                        const ImVec2 point(middle.x + dx, middle.y + dy);
                        if (App().Layout().View().Pick(static_cast<int>(point.x - min.x), static_cast<int>(point.y - min.y)) == frame)
                            return point;
                    }
            return std::nullopt;
        });
        IM_CHECK_SILENT(at.has_value());
        ctx->LogInfo("frame at (%.0f, %.0f)", at->x, at->y);
        // The shortcut menu on the frame: Change Representation > No Representation.
        auto noRepresentation = [ctx, &at] {
            ctx->MouseMoveToPos(*at);
            ctx->MouseClick(1);
            ctx->Yield(3);
            ctx->SetRef("//$FOCUSED");
            ctx->MenuClick("Change Representation");
            ctx->ItemClick("//###Menu_00/No Representation");
            ctx->Yield(3);
        };
        // Not selected (the selection cleared): the frame right-clicked changes.
        ctx->MouseMoveToPos(*at);
        ctx->KeyPress(ImGuiKey_A);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
        ctx->Yield(2);
        IM_CHECK_SILENT(App().State().objectSelection.objects.empty());
        noRepresentation();
        IM_CHECK_EQ(Representation(frame), 0);
        IM_CHECK(App().State().objectSelection.objects.empty());
        Undo(ctx);
        IM_CHECK_EQ(Representation(frame), 1);
        // Selected (a click on its icon): the selection's frames change.
        ctx->MouseMoveToPos(*at);
        ctx->MouseClick(0);
        ctx->Yield(3);
        IM_CHECK(App().State().objectSelection.Contains(frame));
        noRepresentation();
        IM_CHECK_EQ(Representation(frame), 0);
        Undo(ctx);
        IM_CHECK_EQ(Representation(frame), 1);
        Undo(ctx);
        IM_CHECK(Discarded(ctx, frame));
        IM_CHECK(!App().State().document->Dirty());
    };

    // 5 in the 3D Layout: a new Grid, its Setup; the Priority typed in;
    // Ctrl+Z gives it back, Ctrl+Z again takes the grid out.
    t = IM_REGISTER_TEST(engine, "create", "grid_priority_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        const CK_ID grid = CreateInLayout(ctx, ImGuiKey_5, CKCID_GRID);
        IM_CHECK_SILENT(grid != 0);
        IM_CHECK_SILENT(ShowSetup(ctx, grid, "//###setupgrid"));
        auto priority = [grid] { return Object<CKGrid>(grid)->GetGridPriority(); };
        const int before = Read<int>(ctx, priority);
        if (!ctx->ItemExists("**/Priority")) {
            ImGuiWindow *setup = ctx->GetWindowByRef("//###setupgrid");
            for (ImGuiWindow *window : ctx->UiContext->Windows)
                if (window->ParentWindow == setup && std::strstr(window->Name, "/page_")) {
                    ctx->ScrollToBottom(window->ID);
                    ctx->Yield(2);
                    break;
                }
        }
        ctx->ItemInputValue("**/Priority", before + 3);
        ctx->Yield(3);
        IM_CHECK_EQ(Read<int>(ctx, priority), before + 3);
        Undo(ctx);
        IM_CHECK_EQ(Read<int>(ctx, priority), before);
        Undo(ctx);
        IM_CHECK(Discarded(ctx, grid));
        IM_CHECK(!App().State().document->Dirty());
    };

    // R in the Level Manager: a new Array, its Setup; Add Column (OK),
    // Add Row, the cell typed in; Ctrl+Z three times empties it again, once
    // more takes the array out.
    t = IM_REGISTER_TEST(engine, "create", "array_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::set<CK_ID> before = ObjectsOf(ctx, CKCID_DATAARRAY);
        ctx->KeyPress(ImGuiKey_R);
        ctx->Yield(3);
        const CK_ID array = MadeSince(ctx, CKCID_DATAARRAY, before);
        IM_CHECK_SILENT(array != 0);
        IM_CHECK_SILENT(ShowSetup(ctx, array, "//###setuparray"));
        auto size = [array] {
            CKDataArray *a = Object<CKDataArray>(array);
            return std::make_pair(a->GetColumnCount(), a->GetRowCount());
        };
        auto cell = [array] {
            CKDataArray *a = Object<CKDataArray>(array);
            char text[64] = "";
            if (a->GetColumnCount() > 0 && a->GetRowCount() > 0 && a->GetElementStringValue(0, 0, nullptr, 0) < 64)
                a->GetElementStringValue(0, 0, text, sizeof(text));
            return std::string(text);
        };
        IM_CHECK_SILENT((Read<std::pair<int, int>>(ctx, size) == std::make_pair(0, 0)));
        ctx->ItemClick("**/Add Column");
        ctx->Yield(2);
        ctx->ItemClick("//$FOCUSED/OK");
        ctx->Yield(3);
        IM_CHECK((Read<std::pair<int, int>>(ctx, size) == std::make_pair(1, 0)));
        ctx->ItemClick("**/Add Row");
        ctx->Yield(3);
        IM_CHECK((Read<std::pair<int, int>>(ctx, size) == std::make_pair(1, 1)));
        ctx->ItemInputValue("**/##cell", "42");
        ctx->Yield(3);
        IM_CHECK_STR_EQ(Read<std::string>(ctx, cell).c_str(), "42");
        Undo(ctx);
        IM_CHECK(Read<std::string>(ctx, cell) != "42");
        Undo(ctx);
        IM_CHECK((Read<std::pair<int, int>>(ctx, size) == std::make_pair(1, 0)));
        Undo(ctx);
        IM_CHECK((Read<std::pair<int, int>>(ctx, size) == std::make_pair(0, 0)));
        Undo(ctx);
        IM_CHECK(Discarded(ctx, array));
        IM_CHECK(!App().State().document->Dirty());
    };

    // A 3D object's row selected in the Level Manager, G: a new Group with
    // it, its Setup shows the group; Ctrl+Z takes the group out.
    t = IM_REGISTER_TEST(engine, "create", "group_undo");
    t->GuiFunc = MainThreadGui;
    t->TestFunc = [](ImGuiTestContext *ctx) {
        Settle(ctx, "//Level Manager");
        const std::optional<LevelRow> row = ClickLevelRow(ctx, CKCID_3DOBJECT, true);
        IM_CHECK_SILENT(row.has_value());
        const std::set<CK_ID> before = ObjectsOf(ctx, CKCID_GROUP);
        ctx->KeyPress(ImGuiKey_G);
        ctx->Yield(3);
        const CK_ID group = MadeSince(ctx, CKCID_GROUP, before);
        IM_CHECK_SILENT(group != 0);
        const CK_ID member = row->object;
        auto inGroup = [member, group] { return Object<CKBeObject>(member)->IsInGroup(Object<CKGroup>(group)) != 0; };
        IM_CHECK(Read<bool>(ctx, inGroup));
        IM_CHECK(ShowSetup(ctx, group, "//###setupgroup"));
        IM_CHECK(App().State().document->Dirty());
        Undo(ctx);
        IM_CHECK(Discarded(ctx, group));
        IM_CHECK(!App().State().document->Dirty());
    };
}

} // namespace vdevuitest
