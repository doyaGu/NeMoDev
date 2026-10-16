// The helpers the UI tests' categories share (UiTests.h): the Level
// Manager's rows, the 3D Layout's entities, the Setups, objects read on the
// editor's thread.

#include "tests/ui/UiTests.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "core/level/SceneCommands.h"

namespace vdevuitest {

// ---------------------------------------------------------------- objects

std::string ObjectName(ImGuiTestContext *ctx, CK_ID id) {
    return Read<std::string>(ctx, [id] {
        CKObject *object = App().State().host.Context()->GetObject(id);
        return std::string(object && object->GetName() ? object->GetName() : "");
    });
}

bool ObjectExists(ImGuiTestContext *ctx, CK_ID id) {
    return Read<bool>(ctx, [id] { return App().State().host.Context()->GetObject(id) != nullptr; });
}

VxVector Position(ImGuiTestContext *ctx, CK_ID entity) {
    return Read<VxVector>(ctx, [entity] {
        VxVector position(0.0f, 0.0f, 0.0f);
        if (CK3dEntity *object = Object<CK3dEntity>(entity)) object->GetPosition(&position);
        return position;
    });
}

int ScriptCount(ImGuiTestContext *ctx, CK_ID owner) {
    return Read<int>(ctx, [owner] {
        CKBeObject *object = Object<CKBeObject>(owner);
        return object ? object->GetScriptCount() : -1;
    });
}

bool HasInitialConditions(ImGuiTestContext *ctx, CK_ID id) {
    return Read<bool>(ctx, [id] {
        CKScene *scene = vdev::InitialConditionsScene(App().State().host.Context());
        CKSceneObject *object = Object<CKSceneObject>(id);
        return scene && object && scene->GetObjectInitialValue(object) != nullptr;
    });
}

// ---------------------------------------------------------------- Schematic

std::optional<std::pair<CK_ID, ImVec2>> EmptySchematicPoint(CK_ID in, const ImVec2 &room) {
    const std::vector<vdev::SchematicCanvas::ShownScript> scripts = App().Canvas().ShownScripts();
    const std::vector<vdev::SchematicCanvas::ShownBehavior> behaviors = App().Canvas().ShownBehaviors();
    for (const auto &script : scripts) {
        if (in && script.script != in) continue;
        std::optional<std::pair<CK_ID, ImVec2>> best;
        float bestDistance = -1.0f;
        for (float y = std::ceil(script.min.y + 10.0f); y < script.max.y - room.y; y += 8.0f)
            for (float x = std::ceil(script.min.x + 10.0f); x < script.max.x - room.x; x += 8.0f) {
                float nearest = 1e30f;
                for (const vdev::SchematicCanvas::ShownBehavior &shown : behaviors) {
                    const float dx = std::max({shown.min.x - x, 0.0f, x - shown.max.x});
                    const float dy = std::max({shown.min.y - y, 0.0f, y - shown.max.y});
                    nearest = std::min(nearest, dx * dx + dy * dy);
                }
                if (nearest > bestDistance) {
                    bestDistance = nearest;
                    best = std::make_pair(script.script, ImVec2(x, y));
                }
            }
        if (best) return best;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- Level Manager

std::optional<LevelRow> FindLevelRow(ImGuiTestContext *ctx, CK_CLASSID cid, bool exact) {
    for (int pass = 0; pass < 12; ++pass) {
        const std::vector<LevelRow> rows = App().Panels().ShownLevelRows();
        const LevelRow *found = nullptr;
        OnMainThread(ctx, [&] {
            for (const LevelRow &row : rows)
                if (row.object && row.cid && (exact ? row.cid == cid : CKIsChildClassOf(row.cid, cid) != 0)) {
                    found = &row;
                    return;
                }
        });
        if (found) return *found;
        const LevelRow *closed = nullptr;
        for (const LevelRow &row : rows)
            if (row.children && !row.open && (!closed || row.depth < closed->depth)) closed = &row;
        if (!closed) break;
        ctx->MouseMoveToPos(Center(closed->arrowMin, closed->arrowMax));
        ctx->MouseClick(0);
        ctx->Yield(2);
    }
    return std::nullopt;
}

std::optional<LevelRow> ClickLevelRow(ImGuiTestContext *ctx, CK_CLASSID cid, bool exact) {
    std::optional<LevelRow> row = FindLevelRow(ctx, cid, exact);
    if (!row) return row;
    ctx->LogInfo("row '%s'", row->name.c_str());
    // On the name, right of the arrow and the icon.
    ctx->MouseMoveToPos(ImVec2(row->arrowMax.x > 0.0f ? row->arrowMax.x + 40.0f : row->min.x + 120.0f,
                               (row->min.y + row->max.y) * 0.5f));
    ctx->MouseClick(0);
    ctx->Yield(3);
    return row;
}

// ---------------------------------------------------------------- 3D Layout

// Where a click picks the entity: its picked points' one nearest their middle.
InView FindEntity(ImGuiTestContext *ctx, CK_ID wanted) {
    InView found;
    OnMainThread(ctx, [&found, wanted] {
        ImVec2 min, max;
        if (!App().Layout().ShownView(min, max)) return;
        const vdev::LayoutView &view = App().Layout().View();
        CKContext *context = App().State().host.Context();
        std::map<CK_ID, std::vector<ImVec2>> hits;
        // The test engine submits integer mouse pixels. Pick those same
        // pixels instead of a fractional point rounded only when clicked.
        for (float y = std::ceil(min.y + 3.0f); y < max.y - 3.0f; y += 6.0f)
            for (float x = std::ceil(min.x + 3.0f); x < max.x - 3.0f; x += 6.0f) {
                const CK_ID picked = view.Pick(static_cast<int>(x - min.x), static_cast<int>(y - min.y));
                if (picked && (!wanted || picked == wanted) && CK3dEntity::Cast(context->GetObject(picked)))
                    hits[picked].emplace_back(x, y);
            }
        size_t best = 0;
        for (const auto &hit : hits) {
            if (hit.second.size() <= best) continue;
            best = hit.second.size();
            ImVec2 middle(0.0f, 0.0f);
            for (const ImVec2 &point : hit.second) middle = ImVec2(middle.x + point.x, middle.y + point.y);
            middle = ImVec2(middle.x / best, middle.y / best);
            float nearest = 1e30f;
            for (const ImVec2 &point : hit.second) {
                const float d = (point.x - middle.x) * (point.x - middle.x) + (point.y - middle.y) * (point.y - middle.y);
                if (d < nearest) {
                    nearest = d;
                    found.at = point;
                }
            }
            found.entity = hit.first;
            CKObject *object = context->GetObject(hit.first);
            found.name = object && object->GetName() ? object->GetName() : "";
        }
    });
    return found;
}

bool SelectEntity(ImGuiTestContext *ctx, InView &found) {
    ImVec2 min, max;
    if (!App().Layout().ShownView(min, max)) return false;
    ctx->MouseMoveToPos(Center(min, max));
    ctx->KeyPress(ImGuiKey_A);
    ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_D);
    ctx->KeyPress(ImGuiKey_W);
    ctx->Yield(3);
    found = FindEntity(ctx);
    if (!found.entity) return false;
    ctx->LogInfo("entity '%s' at (%.0f, %.0f)", found.name.c_str(), found.at.x, found.at.y);
    ctx->MouseMoveToPos(found.at);
    ctx->MouseClick(0);
    ctx->Yield(3);
    return true;
}

// ---------------------------------------------------------------- Setups

bool ShowsObject(CK_ID object) {
    for (const vdev::SetupWindows::OpenSetup &setup : App().Setups().OpenSetups())
        if (setup.object == object) return true;
    return false;
}

bool ShowSetup(ImGuiTestContext *ctx, CK_ID id, const char *ref) {
    OnMainThread(ctx, [id] { App().State().events.Post(vdev::ShowObjectSetup{id}); });
    ctx->Yield(3);
    ImGuiWindow *window = ctx->GetWindowByRef(ref);
    if (!window || !window->WasActive || !ShowsObject(id)) return false;
    ctx->LogInfo("%s: '%s'", ref, ObjectName(ctx, id).c_str());
    ctx->SetRef(ref);
    return true;
}

CK_ID OpenSetupOf(ImGuiTestContext *ctx, CK_CLASSID cid, const char *ref, bool exact) {
    Settle(ctx);
    const CK_ID id = Read<CK_ID>(ctx, [cid, exact] {
        const XObjectPointerArray &list = App().State().host.Context()->GetObjectListByType(cid, exact ? FALSE : TRUE);
        for (int i = 0; i < list.Size(); ++i)
            if (CKObject *object = list[i])
                if (!object->IsPrivate() && !object->IsDynamic() && !App().State().document->IsDiscarded(object->GetID()))
                    return object->GetID();
        return CK_ID(0);
    });
    return id && ShowSetup(ctx, id, ref) ? id : 0;
}

namespace {

// The Setup page's combo opened: its list's window (focused), else null.
ImGuiWindow *OpenSetupCombo(ImGuiTestContext *ctx, const char *ref, const char *combo) {
    ImGuiWindow *setup = ctx->GetWindowByRef(ref);
    ImGuiWindow *page = nullptr;
    for (ImGuiWindow *window : ctx->UiContext->Windows)
        if (window->ParentWindow == setup && std::strstr(window->Name, "/page_")) page = window;
    if (!page) return nullptr;
    ctx->ItemClick(ImHashStr(combo, 0, page->ID));
    return ctx->GetWindowByRef("//$FOCUSED");
}

} // namespace

void ChooseInCombo(ImGuiTestContext *ctx, const char *ref, const char *combo, const char *item) {
    // The list by its name: "**/" does not read "//$FOCUSED" (ComboClick's way).
    ImGuiWindow *popup = OpenSetupCombo(ctx, ref, combo);
    IM_CHECK_SILENT(popup != nullptr);
    ctx->ItemClick((std::string("//") + popup->Name + "/**/" + item).c_str());
    ctx->Yield(3);
}

void TypeInCombo(ImGuiTestContext *ctx, const char *ref, const char *combo, const char *chars, bool choose) {
    IM_CHECK_SILENT(OpenSetupCombo(ctx, ref, combo) != nullptr);
    ctx->KeyChars(chars);
    ctx->Yield(3);
    ctx->KeyPress(choose ? ImGuiKey_Enter : ImGuiKey_Escape);
    ctx->Yield(3);
}

} // namespace vdevuitest
