// Linking from a multiselection and exported parameters (Dev documentation,
// link.html): Shift+drag export/re-import of pins, local parameters dragged
// across graph borders, links from several selected pins at once.

#include "ui/schematic/SchematicCanvas.h"

#include <algorithm>
#include <cmath>

#include "features/schematic/SchematicPosition.h"

namespace vdev {

void SchematicCanvas::BeginExportDrag() {
    m_Doc.exportPins.clear();
    m_Doc.exportAddresses.clear();
    if (!m_Doc.press.view || m_Doc.press.ref.kind != ElementKind::Pin) return;
    const uint32_t script = m_Doc.press.view->id;
    const bool group = IsSelected(script, m_Doc.press.ref);
    for (const auto &pin : m_Doc.press.view->model.pins) {
        if (pin.onOperation || pin.kind == PinKind::Target) continue;
        const bool pressed = pin.ref == m_Doc.press.ref;
        if (pressed || (group && pin.kind == m_Doc.press.ref.pin && IsSelected(script, pin.ref))) m_Doc.exportPins.push_back(pin);
    }
    if (m_Doc.exportPins.empty()) return;
    for (const auto &pin : m_Doc.exportPins) {
        auto hit = m_Doc.press;
        hit.ref = pin.ref;
        ElementAddress address;
        RetainHit(hit, address);
        if (!address) {
            m_Doc.exportPins.clear();
            m_Doc.exportAddresses.clear();
            return;
        }
        m_Doc.exportAddresses.push_back(address);
    }
    m_Doc.exportScript = script;
    m_Tool = Tool::ExportDrag;
}

void SchematicCanvas::UpdateExportDrag(const ImVec2 &mouse) {
    ScriptView *view = ViewFor(m_Doc.exportScript);
    if (!view) {
        m_Tool = Tool::Idle;
        return;
    }
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    for (const auto &pin : m_Doc.exportPins) {
        const ImVec2 from = view->transform.ToScreen(pin.center.h, pin.center.v + view->offsetY);
        drawList->AddLine(from, mouse, m_Style.highlightLink, 2.0f);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) EndExportDrag(mouse);
}

void SchematicCanvas::EndExportDrag(const ImVec2 &mouse) {
    m_Tool = Tool::Idle;
    ScriptView *view = ViewFor(m_Doc.exportScript);
    if (!view)
        return;
    const Point world = view->transform.ToWorld(mouse);
    const Point local(world.h, world.v - view->offsetY);
    EditorField elements{"elements", EDITOR_RECORD}, at{"at", EDITOR_REAL_LIST};
    for (const auto &address : m_Doc.exportAddresses)
        elements.fields.push_back(SchematicPosition(address, std::to_string(elements.fields.size()).c_str()));
    at.reals = {local.h, local.v};
    if (!m_Doc.exportAddresses.empty()) {
        const auto &first = m_Doc.exportAddresses.front();
        std::vector<EditorField> fields{{"target", EDITOR_OBJECT, first.script},
                                        {"revision", EDITOR_INTEGER_VALUE, {}, {}, first.revision},
                                        {"topology", EDITOR_INTEGER_VALUE, {}, {}, first.topology},
                                        std::move(elements),
                                        std::move(at)};
        const auto values = BorrowEditorFields(fields);
        Execute("schematic.export-pins", {values.data(), values.data() + values.size()});
    }
    m_Doc.exportPins.clear();
    m_Doc.exportAddresses.clear();
}

} // namespace vdev
