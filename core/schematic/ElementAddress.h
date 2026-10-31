#pragma once
#include "core/schematic/ElementRef.h"
#include "editor/Identity.h"
#include <cstdint>

namespace vdev {
class Document;

// A retained Schematic position. Native objects carry their lifetime; table
// slots also carry the Script revision at which they were read. Never persist
// this value in a composition or a workspace configuration.
struct ElementAddress {
    ObjectRef script{}, owner{}, object{}, endpoint{};
    ElementRef element;
    std::uint64_t revision = 0, topology = 0;

    static ElementAddress Capture(Document &, CK_ID script, const ElementRef &);
    bool Resolve(Document &, CK_ID &script, ElementRef &) const;
    explicit operator bool() const { return bool(script); }
};
} // namespace vdev
