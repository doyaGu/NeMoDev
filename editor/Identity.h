#pragma once

#include <stdint.h>

// Process-local value identities. Zero is unbound. Neither value owns a native
// object or survives serialization/restarting the editor. Object/lifetime are
// opaque native identity values; this header has no SDK or STL dependency.
#ifdef __cplusplus
namespace vdev {
#endif

typedef struct DocumentToken {
    uint64_t value;
} DocumentToken;

typedef struct ObjectRef {
    DocumentToken document;
    uint32_t object;
    uint64_t lifetime;
#ifdef __cplusplus
    explicit operator bool() const { return document.value != 0 && object != 0 && lifetime != 0; }
#endif
} ObjectRef;

#ifdef __cplusplus
inline bool operator==(DocumentToken a, DocumentToken b) { return a.value == b.value; }
inline bool operator!=(DocumentToken a, DocumentToken b) { return !(a == b); }
inline bool operator==(ObjectRef a, ObjectRef b) {
    return a.document == b.document && a.object == b.object && a.lifetime == b.lifetime;
}
inline bool operator!=(ObjectRef a, ObjectRef b) { return !(a == b); }
inline bool operator<(ObjectRef a, ObjectRef b) {
    if (a.document != b.document)
        return a.document.value < b.document.value;
    return a.object < b.object || (a.object == b.object && a.lifetime < b.lifetime);
}
} // namespace vdev
#endif
