#include "core/schematic/SchematicNavigation.h"
#include "core/document/Document.h"
namespace vdev {
Selection &Selection::operator=(const Selection &other) {
    if (this != &other) {
        script = other.script;
        items = other.items;
        headers = other.headers;
        m_Document = other.m_Document;
        m_Addresses = other.m_Addresses;
        ++m_Revision;
    }
    return *this;
}
void Selection::Rebase() {
    ++m_Revision;
    m_Addresses.clear();
    if (!m_Document) return;
    for (const auto &item : items) m_Addresses.push_back(ElementAddress::Capture(*m_Document, script, item));
    for (auto header : headers) m_Addresses.push_back(ElementAddress::Capture(*m_Document, header, HeaderOf(header)));
}
void Selection::Assign(const std::vector<ElementAddress> &addresses) {
    Clear();
    for (const auto &address : addresses) {
        if (!address)
            continue;
        if (!script)
            script = address.script.object;
        if (address.script.object == script)
            items.push_back(address.element);
        else if (address.element.kind == ElementKind::Script)
            headers.push_back(address.script.object);
        else
            continue;
        m_Addresses.push_back(address);
    }
}
void Selection::Validate() {
    if (!m_Document) { Clear(); return; }
    bool valid = true;
    for (const auto &address : m_Addresses) {
        CK_ID id = 0;
        ElementRef ref;
        if (!address.Resolve(*m_Document, id, ref)) { valid = false; break; }
    }
    if (valid) return;
    const auto addresses = m_Addresses;
    Clear();
    for (const auto &address : addresses) {
        CK_ID id = 0;
        ElementRef ref;
        if (address.Resolve(*m_Document, id, ref)) Add(id, ref);
    }
}
std::set<CK_ID> SchematicNavigation::HiddenIds(const Document &document) const {
    std::set<CK_ID> ids;
    for (const auto &ref : hiddenScripts)
        if (const auto id = document.NativeId(ref)) ids.insert(id);
    return ids;
}
} // namespace vdev
