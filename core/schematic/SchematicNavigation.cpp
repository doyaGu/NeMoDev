#include "core/schematic/SchematicNavigation.h"
#include "core/document/Document.h"
namespace vdev {
Selection &Selection::operator=(const Selection &other) {
    if (this != &other) {
        script = other.script;
        items = other.items;
        headers = other.headers;
        m_Addresses = other.m_Addresses;
        ++m_Revision;
    }
    return *this;
}
void Selection::Rebase() {
    ++m_Revision;
    m_Addresses.clear();
    if (!m_ReadPosition)
        return;
    for (const auto &item : items)
        m_Addresses.push_back(m_ReadPosition(script, item));
    for (auto header : headers)
        m_Addresses.push_back(m_ReadPosition(header, HeaderOf(header)));
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
void Selection::Validate(const std::function<bool(const ElementAddress &)> &valid) {
    std::vector<ElementAddress> retained;
    for (const auto &address : m_Addresses)
        if (valid(address))
            retained.push_back(address);
    if (retained.size() != m_Addresses.size())
        Assign(retained);
}
std::set<CK_ID> SchematicNavigation::HiddenIds(const Document &document) const {
    std::set<CK_ID> ids;
    for (const auto &ref : hiddenScripts)
        if (const auto id = document.NativeId(ref)) ids.insert(id);
    return ids;
}
} // namespace vdev
