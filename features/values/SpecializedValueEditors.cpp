#include "features/values/SpecializedValueEditors.h"
#include "application/EditorApplication.h"
#include "core/catalog/ValueText.h"
#include "ui/controls/DevBitmaps.h"
#include "ui/controls/DevControls.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace vdev {
namespace {
std::string Text(EditorString value) { return {value.data ? value.data : "", value.size}; }
const EditorValue *Field(const EditorValue *fields, uint32_t count, const char *key, EditorValueType type) {
    for (uint32_t i = 0; i < count; ++i) if (Text(fields[i].key) == key && fields[i].type == type) return &fields[i];
    return nullptr;
}
struct Controls {
    const EditorValueInput &input;
    std::vector<EditorField> owned;
    ImVec2 at;
    float scale;
    bool changed = false, accept = false, active = false, valid = true;
    Controls(const EditorValueInput &input, float width) : input(input), at(ImGui::GetCursorScreenPos()),
        scale(input.style == EDITOR_VALUE_DIALOG ? input.width / width : ImGui::GetFontSize() / 13) {
        valid = input.value.type == EDITOR_RECORD && CopyEditorFields(owned, &input.value, 1) == EDITOR_OK;
    }
    EditorField *Value(const char *key, EditorValueType type) {
        if (valid) for (auto &field : owned.front().fields) if (field.key == key && field.type == type) return &field;
        valid = false; return nullptr;
    }
    ImVec2 P(float x, float y) const { return ImVec2(at.x + x * 1.5f * scale, at.y + y * 1.625f * scale); }
    void Observe(bool done = false) {
        active |= ImGui::IsItemActive() || ImGui::IsItemActivated();
        accept |= done || ImGui::IsItemDeactivatedAfterEdit();
    }
    void Label(float x, float y, float right, float bottom, const char *text, vi::Align align = vi::LEFT) {
        vi::Text(P(x, y), P(right, bottom), text, true, align);
    }
    bool Number(const char *id, float x, float y, float width, double &value, double factor = 1) {
        char buffer[64]; std::snprintf(buffer, sizeof(buffer), factor == 100 ? "%g%%" : "%g", value * factor);
        std::string text = buffer; bool done = false, modified = false;
        if (vi::Edit(id, P(x, y), P(x + width, y + 12), text, vi::CENTER, &done)) {
            double parsed = 0;
            if (ParseRealValue(text, parsed, factor)) { value = parsed; changed = modified = true; }
            else valid = false;
        }
        Observe(done); return modified;
    }
    bool Check(const char *id, float x, float y, bool &on) {
        const bool clicked = vi::Check(id, P(x, y), 12 * scale, on); Observe();
        if (clicked) changed = accept = true;
        return clicked;
    }
    bool Bitmap(const char *id, float x, int bitmap, bool on, const char *tip) {
        const auto a = P(x, 2), b = P(x + 11, 12);
        ImGui::SetCursorScreenPos(a); const bool clicked = ImGui::InvisibleButton(id, ImVec2(b.x - a.x, b.y - a.y)); Observe();
        auto *dl = ImGui::GetWindowDrawList();
        if (on || ImGui::IsItemActive()) dl->AddRectFilled(a, b, vi::Color(4));
        if (const auto texture = CK2UIBitmap(bitmap)) dl->AddImage(texture, a, ImVec2(a.x + 16 * scale, a.y + 16 * scale));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        return clicked;
    }
    uint32_t Finish() {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (active) ImGui::ClearActiveID();
            return EDITOR_INPUT_CANCEL;
        }
        if (changed && valid) {
            const auto borrowed = BorrowEditorFields(owned);
            valid = input.update.write && input.update.write(input.update.data, borrowed.data(), 1) == EDITOR_OK;
        }
        return (active ? EDITOR_INPUT_ACTIVE : 0u) | (accept ? EDITOR_INPUT_ACCEPT : 0u) |
            (changed ? EDITOR_INPUT_CHANGED : 0u) | (!valid ? EDITOR_INPUT_INVALID : 0u);
    }
};
struct Drawing {
    Drawing(const EditorValueInput &input) { ImGui::PushID(Text(input.label).c_str()); ImGui::BeginGroup(); }
    ~Drawing() { ImGui::EndGroup(); ImGui::PopID(); }
};
bool NewControl(const EditorValueInput &input) {
    // Keep presentation state in the ImGui window, with fixed keys per control.
    // A reopened control or a different full object reference starts fresh.
    const auto *target = Field(input.choicesRequest.values, input.choicesRequest.count, "target", EDITOR_OBJECT);
    const auto object = target ? target->object : EditorObject{input.choicesRequest.scope.document, 0, input.choicesRequest.scope.view};
    const uint32_t identity[] = {uint32_t(object.document.value), uint32_t(object.document.value >> 32),
        object.object, uint32_t(object.lifetime), uint32_t(object.lifetime >> 32)};
    auto *storage = ImGui::GetStateStorage(); const auto frameKey = ImGui::GetID("last-frame");
    const int frame = ImGui::GetFrameCount(), previous = storage->GetInt(frameKey, -2);
    // Dialog working copies keep their presentation while native work pauses
    // drawing. A new working copy has a different full object reference.
    bool fresh = input.style == EDITOR_VALUE_INLINE && previous != frame && previous != frame - 1;
    for (unsigned i = 0; i < 5; ++i) {
        char name[24]; std::snprintf(name, sizeof(name), "value-owner-%u", i); const auto key = ImGui::GetID(name);
        fresh |= uint32_t(storage->GetInt(key)) != identity[i]; storage->SetInt(key, int(identity[i]));
    }
    storage->SetInt(frameKey, frame); return fresh;
}
bool Preview(const EditorHost *host, const EditorValueInput &input, const std::vector<EditorField> &value,
             std::vector<double> &samples, std::vector<double> &tangents) {
    const auto borrowed = BorrowEditorFields(value); auto argument = borrowed.values.front(); argument.key = EDITOR_TEXT("value");
    const EditorRequest request{input.choicesRequest.scope, &argument, 1, EDITOR_TEXT("builtin.parameters")};
    std::vector<EditorField> fields;
    const auto copy = [](void *data, const EditorValue *values, uint32_t count) { return CopyEditorFields(*static_cast<std::vector<EditorField> *>(data), values, count); };
    if (host->query(host->data, EditorString EDITOR_TEXT("parameter.curve2d-preview"), &request, {&fields, copy}) != EDITOR_OK) return false;
    for (auto &field : fields) {
        if (field.key == "points" && field.type == EDITOR_REAL_LIST) samples = std::move(field.reals);
        if (field.key == "tangents" && field.type == EDITOR_REAL_LIST) tangents = std::move(field.reals);
    }
    return samples.size() == 101;
}
}
uint32_t DrawCurve2DValue(void *, const EditorHost *host, const EditorValueInput *input) {
    const auto *representation = Field(input->fields, input->count, "representation", EDITOR_INTEGER_VALUE);
    const bool collapsed = input->style == EDITOR_VALUE_DIALOG && representation && representation->integer;
    Controls ui(*input, collapsed ? 206 : 213 * 1.5f);
    auto *coordinates = ui.Value("points", EDITOR_REAL_LIST), *masks = ui.Value("flags", EDITOR_INTEGER_LIST);
    if (!ui.valid || !coordinates || !masks || coordinates->reals.size() != masks->integers.size() * 9 || masks->integers.size() > 4096) return EDITOR_INPUT_INVALID;
    auto &points = coordinates->reals; auto &flags = masks->integers;
    Drawing group(*input);
    const bool fresh = NewControl(*input);
    auto *storage = ImGui::GetStateStorage(); const auto chosenKey = ImGui::GetID("chosen"), gridKey = ImGui::GetID("grid"), tangentKey = ImGui::GetID("tangents");
    int selected = fresh ? 0 : std::clamp(storage->GetInt(chosenKey), 0, std::max(0, int(flags.size()) - 1));
    bool hideGrid = !fresh && storage->GetBool(gridKey), hideTangents = !fresh && storage->GetBool(tangentKey);
    if (!collapsed && !flags.empty()) {
        ui.Label(5, 1, 13, 9, "X:"); ui.Label(75, 1, 83, 9, "Y:");
        if (ui.Number("##cx", 15, 1, 55, points[size_t(selected) * 9]))
            points[size_t(selected) * 9] = std::clamp(points[size_t(selected) * 9], 0.0, 1.0);
        ui.Number("##cy", 85, 1, 55, points[size_t(selected) * 9 + 1]);
        if (ui.changed) {
            std::vector<size_t> order(flags.size()); std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return points[a * 9] < points[b * 9]; });
            auto sorted = points; auto sortedFlags = flags;
            for (size_t i = 0; i < order.size(); ++i) {
                std::copy_n(points.begin() + order[i] * 9, 9, sorted.begin() + i * 9); sortedFlags[i] = flags[order[i]];
                if (order[i] == size_t(selected)) storage->SetInt(chosenKey, int(i));
            }
            points = std::move(sorted); flags = std::move(sortedFlags); selected = storage->GetInt(chosenKey);
        }
        if (ui.Bitmap("##tangents", 144, 2010, hideTangents, "Hide Tangents")) hideTangents = !hideTangents;
        const bool linear = (flags[size_t(selected)] & 2) != 0;
        if (ui.Bitmap("##linear", 159, linear ? 2012 : 2011, linear, "Linear")) {
            flags[size_t(selected)] ^= 2; ui.changed = ui.accept = true;
        }
        if (ui.Bitmap("##grid", 174, 2013, !hideGrid, "Grid")) hideGrid = !hideGrid;
    }
    auto *dl = ImGui::GetWindowDrawList();
    const auto a = collapsed ? ImVec2(ui.at.x + 3 * ui.scale, ui.at.y + 3 * ui.scale) : ui.P(3, 16);
    const auto b = collapsed ? ImVec2(a.x + 200 * ui.scale, a.y + 40 * ui.scale) : ui.P(208, 164);
    const float margin = (collapsed ? 1 : 6) * ui.scale;
    const ImVec2 g0(a.x + margin, a.y + margin), g1(b.x - margin, b.y - margin);
    auto screen = [&](double x, double y) { return ImVec2(g0.x + float(x) * (g1.x - g0.x), g1.y - float(y) * (g1.y - g0.y)); };
    dl->AddRectFilled(a, b, vi::Color(8)); dl->AddRect(a, b, vi::Color(3));
    if (!collapsed && !hideGrid) for (int i = 1; i < 4; ++i) {
        const auto x = g0.x + (g1.x - g0.x) * i / 4, y = g0.y + (g1.y - g0.y) * i / 4;
        dl->AddLine(ImVec2(x, g0.y), ImVec2(x, g1.y), vi::Color(7)); dl->AddLine(ImVec2(g0.x, y), ImVec2(g1.x, y), vi::Color(7));
    }
    if (!collapsed) {
        dl->AddRect(g0, g1, vi::Color(5));
        ImGui::SetCursorScreenPos(a); ImGui::InvisibleButton("##curve", ImVec2(b.x - a.x, b.y - a.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight); ui.Observe();
        const auto mouse = ImGui::GetIO().MousePos;
        if (ImGui::IsItemActivated()) for (size_t i = 0; i < flags.size(); ++i) {
            const auto p = screen(points[i * 9], points[i * 9 + 1]);
            if (std::abs(p.x - mouse.x) <= 5 * ui.scale && std::abs(p.y - mouse.y) <= 5 * ui.scale) selected = int(i);
        }
        double x = std::clamp(double((mouse.x - g0.x) / (g1.x - g0.x)), 0.0, 1.0), y = std::clamp(double((g1.y - mouse.y) / (g1.y - g0.y)), 0.0, 1.0);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 1) && !flags.empty()) {
            const auto i = size_t(selected);
            const double left = i ? points[(i - 1) * 9] : 0, right = i + 1 < flags.size() ? points[(i + 1) * 9] : 1;
            points[i * 9] = i == 0 ? 0 : i + 1 == flags.size() ? 1 : std::clamp(x, left, right); points[i * 9 + 1] = y;
            ui.changed = true; ImGui::MarkItemEdited(ImGui::GetItemID());
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && flags.size() < 4096) {
            size_t i = 0; while (i < flags.size() && points[i * 9] < x) ++i;
            const double point[] = {x, y, 0, 0, 0, 0, 0, 0, 0};
            points.insert(points.begin() + i * 9, std::begin(point), std::end(point)); flags.insert(flags.begin() + i, 0); selected = int(i);
            ui.changed = ui.accept = true;
        }
        if ((ImGui::IsItemClicked(1) || (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Delete))) && flags.size() > 2) {
            points.erase(points.begin() + selected * 9, points.begin() + (selected + 1) * 9); flags.erase(flags.begin() + selected);
            selected = std::max(0, selected - 1); ui.changed = ui.accept = true;
        }
    }
    std::vector<double> samples, tangents;
    if (!Preview(host, *input, ui.owned, samples, tangents) || tangents.size() != flags.size() * 4) ui.valid = false;
    if (samples.size() == 101) for (size_t i = 1; i < samples.size(); ++i) dl->AddLine(screen(double(i - 1) / 100, samples[i - 1]), screen(double(i) / 100, samples[i]), vi::Color(0), (collapsed ? 1 : 1.5f) * ui.scale);
    if (!collapsed) for (size_t i = 0; i < flags.size(); ++i) {
        const auto p = screen(points[i * 9], points[i * 9 + 1]); const float h = 3 * ui.scale;
        if (!hideTangents && !(flags[i] & 2) && tangents.size() == flags.size() * 4) {
            dl->AddLine(p, screen(points[i * 9] - tangents[i * 4] * .15, points[i * 9 + 1] - tangents[i * 4 + 1] * .15), vi::Color(5));
            dl->AddLine(p, screen(points[i * 9] + tangents[i * 4 + 2] * .15, points[i * 9 + 1] + tangents[i * 4 + 3] * .15), vi::Color(5));
        }
        dl->AddRectFilled(ImVec2(p.x - h, p.y - h), ImVec2(p.x + h, p.y + h), int(i) == selected ? vi::Color(10) : vi::Color(1));
    }
    storage->SetInt(chosenKey, selected); storage->SetBool(gridKey, hideGrid); storage->SetBool(tangentKey, hideTangents);
    if (input->style == EDITOR_VALUE_INLINE) { ImGui::SetCursorScreenPos(ui.at); ImGui::Dummy(ImVec2(213 * 1.5f * ui.scale, 169 * 1.625f * ui.scale)); }
    return ui.Finish();
}
uint32_t DrawDependenciesValue(void *, const EditorHost *, const EditorValueInput *input) {
    Controls ui(*input, 296 * 1.5f);
    auto *mode = ui.Value("mode", EDITOR_INTEGER_VALUE), *options = ui.Value("options", EDITOR_INTEGER_LIST);
    const auto *schema = Field(input->fields, input->count, "schema", EDITOR_RECORD);
    if (!ui.valid || !mode || !options || !schema) return EDITOR_INPUT_INVALID;
    Drawing group(*input); const bool fresh = NewControl(*input); auto *storage = ImGui::GetStateStorage();
    const auto selectedKey = ImGui::GetID("class"); int selected = fresh ? -1 : storage->GetInt(selectedKey, -1);
    const char *names[] = {"No Dependencies", "Full Dependencies", "Custom Dependencies"}; const unsigned modes[] = {1, 2, 0};
    for (unsigned i = 0; i < 3; ++i) {
        ImGui::PushID(int(i));
        if (vi::Radio("##radio", ui.P(7, 3 + 10 * float(i)), 12 * ui.scale, mode->integer == modes[i], names[i])) {
            mode->integer = modes[i]; ui.changed = ui.accept = true;
        }
        ui.Observe(); ImGui::PopID();
    }
    if (mode->integer == 0) {
        const auto min = ui.P(5, 35), max = ui.P(125, 190); auto *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(min, max, vi::Color(9)); dl->AddRect(min, max, vi::Color(0));
        ImGui::SetCursorScreenPos(ImVec2(min.x + 1, min.y + 1));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2 * ui.scale, 2 * ui.scale));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::BeginChild("##tree", ImVec2(max.x - min.x - 2, max.y - min.y - 2));
        const EditorValue *chosen = nullptr; int hiddenBelow = -1;
        for (uint32_t i = 0; i < schema->fields.count; ++i) {
            const auto &entry = schema->fields.data[i];
            const auto *id = Field(entry.fields.data, entry.fields.count, "id", EDITOR_INTEGER_VALUE), *depth = Field(entry.fields.data, entry.fields.count, "depth", EDITOR_INTEGER_VALUE), *name = Field(entry.fields.data, entry.fields.count, "name", EDITOR_TEXT_VALUE);
            if (entry.type != EDITOR_RECORD || !id || id->integer >= 65536 || !depth || depth->integer > 64 || !name) { ui.valid = false; break; }
            if (selected < 0) selected = int(id->integer);
            if (selected == int(id->integer)) chosen = &entry;
            const int level = int(depth->integer);
            if (hiddenBelow >= 0 && level > hiddenBelow) continue;
            hiddenBelow = -1;
            const auto *next = i + 1 < schema->fields.count ? &schema->fields.data[i + 1] : nullptr;
            const auto *nextDepth = next ? Field(next->fields.data, next->fields.count, "depth", EDITOR_INTEGER_VALUE) : nullptr;
            const bool parent = nextDepth && nextDepth->integer > depth->integer;
            ImGui::PushID(int(id->integer)); const auto foldedKey = ImGui::GetID("folded"); bool folded = storage->GetBool(foldedKey);
            if (fresh) { folded = false; storage->SetBool(foldedKey, false); }
            if (parent && folded) hiddenBelow = level;
            const auto cursor = ImGui::GetCursorScreenPos(); const auto x = cursor.x + 19 * ui.scale * level; const auto line = 16 * ui.scale;
            auto *tree = ImGui::GetWindowDrawList();
            if (parent) {
                const ImVec2 a(x + 2 * ui.scale, cursor.y + (line - 9 * ui.scale) / 2), b(a.x + 9 * ui.scale, a.y + 9 * ui.scale);
                ImGui::SetCursorScreenPos(a);
                if (ImGui::InvisibleButton("##fold", ImVec2(9 * ui.scale, 9 * ui.scale))) storage->SetBool(foldedKey, !folded);
                tree->AddRectFilled(a, b, vi::Color(9)); tree->AddRect(a, b, vi::Color(5));
                const auto cy = (a.y + b.y) / 2, cx = (a.x + b.x) / 2;
                tree->AddLine(ImVec2(a.x + 2 * ui.scale, cy), ImVec2(b.x - 2 * ui.scale, cy), vi::Color(0));
                if (folded) tree->AddLine(ImVec2(cx, a.y + 2 * ui.scale), ImVec2(cx, b.y - 2 * ui.scale), vi::Color(0));
            }
            const auto text = Text(name->text); const ImVec2 at(x + 15 * ui.scale, cursor.y + (line - ImGui::GetFontSize()) / 2);
            const auto width = ImGui::CalcTextSize(text.c_str()).x;
            ImGui::SetCursorScreenPos(ImVec2(at.x - 2 * ui.scale, cursor.y));
            if (ImGui::InvisibleButton("##class", ImVec2(width + 4 * ui.scale, line))) { selected = int(id->integer); chosen = &entry; }
            const bool on = selected == int(id->integer);
            if (on) tree->AddRectFilled(ImVec2(at.x - 2 * ui.scale, cursor.y + 1), ImVec2(at.x + width + 2 * ui.scale, cursor.y + line - 1), IM_COL32(10, 36, 106, 255));
            tree->AddText(at, on ? vi::Color(9) : vi::Color(0), text.c_str());
            ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + line)); ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(1, 1)); ImGui::EndChild(); ImGui::PopStyleVar(2); ImGui::PopStyleColor();
        const auto *labels = chosen ? Field(chosen->fields.data, chosen->fields.count, "labels", EDITOR_TEXT_LIST) : nullptr;
        if (labels && selected >= 0) for (uint32_t i = 0; i < labels->texts.count && i < 32; ++i) {
            const auto mask = uint64_t(1) << i; auto bits = size_t(selected) < options->integers.size() ? options->integers[size_t(selected)] : 0;
            bool on = (bits & mask) != 0; ImGui::PushID(int(i));
            const auto x = i % 2 ? 218.0f : 132.0f, y = i == 0 ? 33.0f : 32.0f + 10.0f * (i / 2);
            if (ui.Check("##option", x, y + 1, on)) {
                if (size_t(selected) >= options->integers.size()) options->integers.resize(size_t(selected) + 1);
                options->integers[size_t(selected)] = on ? bits | mask : bits & ~mask;
            }
            vi::Text(ui.P(x + 11, y), ui.P(x + 78, y + 10), Text(labels->texts.data[i]).c_str(), false, vi::LEFT); ImGui::PopID();
        }
    }
    storage->SetInt(selectedKey, selected);
    if (input->style == EDITOR_VALUE_INLINE) { ImGui::SetCursorScreenPos(ui.at); ImGui::Dummy(ImVec2(296 * 1.5f * ui.scale, (mode->integer == 0 ? 196 * 1.625f : 196 * 1.625f - 260) * ui.scale)); }
    return ui.Finish();
}
uint32_t DrawLodOptionsValue(void *, const EditorHost *, const EditorValueInput *input) {
    Controls ui(*input, 231 * 1.5f); auto *flags = ui.Value("flags", EDITOR_INTEGER_VALUE), *values = ui.Value("proportions", EDITOR_REAL_LIST);
    if (!ui.valid || !flags || !values || values->reals.size() != 6) return EDITOR_INPUT_INVALID;
    Drawing group(*input); auto bits = uint32_t(flags->integer);
    auto flag = [&](const char *id, float x, float y, uint32_t mask, const char *name, float textX) {
        bool on = (bits & mask) != 0;
        if (ui.Check(id, x, y, on)) bits = on ? bits | mask : bits & ~mask;
        ui.Label(textX, y, 99, y + 10, name);
    };
    const bool animation = (bits & 4) != 0, alpha = (bits & 1) != 0;
    ImGui::GetWindowDrawList()->AddRect(ui.P(5, 3), ui.P(100, 39), vi::Color(5), 3 * ui.scale);
    if (!animation) flag("##meshes", 13, 8, 2, "Multiple Meshes", 28);
    flag("##animation", 13, 18, 4, "Character Animation", 28);
    if (!animation) flag("##patch", 13, 28, 8, "Patch Mesh", 27);
    flag("##alpha", 13, 42, 1, "Alpha", 28);
    ui.Label(140, 1, 170, 11, "Mag", vi::CENTER); ui.Label(185, 1, 215, 11, "Min", vi::CENTER);
    ui.Label(105, 10, 135, 20, "Screen:", vi::RIGHT); ui.Label(105, 26, 135, 36, animation ? "Anims:" : "Faces:", vi::RIGHT);
    ui.Number("##screenmag", 140, 10, 30, values->reals[0], 100); ui.Number("##screenmin", 185, 10, 30, values->reals[1], 100);
    ui.Number("##facesmag", 140, 25, 30, values->reals[2], 100); ui.Number("##facesmin", 185, 25, 30, values->reals[3], 100);
    if (alpha) {
        ui.Label(80, 41, 135, 51, "Screen Alpha:", vi::RIGHT);
        ui.Number("##alphamag", 140, 41, 30, values->reals[4], 100); ui.Number("##alphamin", 185, 41, 30, values->reals[5], 100);
    }
    flags->integer = uint64_t(int64_t(int32_t(bits)));
    if (input->style == EDITOR_VALUE_INLINE) { ImGui::SetCursorScreenPos(ui.at); ImGui::Dummy(ImVec2(231 * 1.5f * ui.scale, 55 * 1.625f * ui.scale)); }
    return ui.Finish();
}
}
