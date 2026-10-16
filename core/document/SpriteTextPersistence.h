#pragma once

#include <map>
#include <vector>

#include "CKAll.h"

namespace vdev {

class Document;
struct DuplicateName;

// Editor-owned interface chunks, with CK object references (remapped on load).
void WriteSpriteTextAlignments(Document &document);
// Only managers and sprites loaded by this file; keep-current duplicates stay intact.
void RestoreSpriteTextAlignments(CKFile &file, const std::vector<DuplicateName> *duplicates = nullptr);
// Keyed by saved IDs, for the saver's independent-context comparison.
std::map<CK_ID, int> CaptureSpriteTextAlignments(CKFile &file);

} // namespace vdev
