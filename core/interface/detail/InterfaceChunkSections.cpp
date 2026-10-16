#include "core/interface/detail/InterfaceChunkSections.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace {
bool ReadList(const std::vector<CKDWORD> &buffer, size_t &cursor, std::vector<int> &list) {
    if (cursor >= buffer.size()) return false;
    const CKDWORD count = buffer[cursor++];
    if (count > buffer.size() - cursor) return false;
    list.reserve(count);
    for (CKDWORD i = 0; i < count; ++i) list.push_back(static_cast<int>(buffer[cursor++]));
    return true;
}

bool Fits(const std::vector<CKDWORD> &payload, size_t start, size_t count) {
    return start <= payload.size() && count <= payload.size() - start;
}

// Sub-chunk records have a DWORD count prefix. Their internal reference lists
// remain relative to the sub-chunk, so relocating the parent does not edit them.
bool SubChunksFit(const std::vector<CKDWORD> &payload, size_t cursor, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!Fits(payload, cursor, 1)) return false;
        const CKDWORD words = payload[cursor++];
        if (!Fits(payload, cursor, words)) return false;
        cursor += words;
    }
    return true;
}
} // namespace

CKERROR InterfaceChunkSections::Read(CKStateChunk *chunk,
                                     std::shared_ptr<InterfaceChunkSections> &result) {
    result.reset();
    // ConvertToBuffer closes a parser. This operation is performed before the
    // InterfaceData reader starts, or after its writer has closed the chunk.
    const int bytes = chunk->ConvertToBuffer(nullptr);
    if (bytes < 8 || bytes % sizeof(CKDWORD)) return CKERR_INVALIDPARAMETER;
    std::vector<CKDWORD> buffer(bytes / sizeof(CKDWORD));
    if (chunk->ConvertToBuffer(buffer.data()) != bytes) return CKERR_INVALIDPARAMETER;
    auto storage = std::make_shared<InterfaceChunkSections>();
    const CKERROR error = storage->Decode(buffer);
    if (error == CK_OK) result = storage;
    return error;
}

CKERROR InterfaceChunkSections::Decode(const std::vector<CKDWORD> &buffer) {
    metadata = buffer[0];
    const CKDWORD chunkVersion = (metadata >> 16) & 0xFF;
    const CKDWORD options = metadata >> 24;
    // CKStateChunk version is distinct from InterfaceChunk's payload version.
    if (chunkVersion != CHUNK_VERSION3 && chunkVersion != CHUNK_VERSION4) return CKERR_NOTIMPLEMENTED;
    if (options & CHNK_OPTION_LISTBIG) return CKERR_NOTIMPLEMENTED;
    const CKDWORD count = buffer[1];
    if (count < 2 || count > buffer.size() - 2) return CKERR_INVALIDPARAMETER;
    const std::vector<CKDWORD> data(buffer.begin() + 2, buffer.begin() + 2 + count);
    std::set<CKDWORD> identifiers;
    size_t start = 0;
    do {
        if (!Fits(data, start, 2)) return CKERR_INVALIDPARAMETER;
        const CKDWORD next = data[start + 1];
        if (next && next >= data.size()) return CKERR_INVALIDPARAMETER;
        const size_t end = next ? next : data.size();
        if (end < start + 2 || end > data.size()) return CKERR_INVALIDPARAMETER;
        Section section;
        section.identifier = data[start];
        section.begin = static_cast<int>(start + 2);
        section.end = static_cast<int>(end);
        // The SDK requires unique identifiers. Reject duplicates instead of
        // selecting one occurrence and silently dropping the others.
        if (!identifiers.insert(section.identifier).second) return CKERR_INVALIDPARAMETER;
        section.payload.assign(data.begin() + start + 2, data.begin() + end);
        sections.push_back(std::move(section));
        start = end;
        if (!next) break;
    } while (start < data.size());
    if (start != data.size()) return CKERR_INVALIDPARAMETER;

    size_t cursor = 2 + count;
    std::vector<int> ids, chunks, managers;
    if ((options & CHNK_OPTION_IDS) && !ReadList(buffer, cursor, ids)) return CKERR_INVALIDPARAMETER;
    if ((options & CHNK_OPTION_CHN) && !ReadList(buffer, cursor, chunks)) return CKERR_INVALIDPARAMETER;
    if ((options & CHNK_OPTION_MAN) && !ReadList(buffer, cursor, managers)) return CKERR_INVALIDPARAMETER;
    if (cursor != buffer.size() || !SplitList(data, ids, CHNK_OPTION_IDS, sections) ||
        !SplitList(data, chunks, CHNK_OPTION_CHN, sections) || !SplitList(data, managers, CHNK_OPTION_MAN, sections))
        return CKERR_INVALIDPARAMETER;
    return CK_OK;
}

bool InterfaceChunkSections::PayloadBounds(CKDWORD identifier, int &begin, int &end) const {
    for (const auto &section : sections) {
        if (section.identifier != identifier) continue;
        begin = section.begin;
        end = section.end;
        return true;
    }
    return false;
}

void InterfaceChunkSections::FinishRead(CKDWORD version, bool nonScript) {
    formatVersion = version;
    rootIsNotScript = nonScript;
    // Known payload is regenerated from the public records. Keep its identifier
    // for ordering, without a second persistent copy of text/bitmap encodings.
    for (auto &section : sections) {
        if (!known.count(section.identifier)) continue;
        std::vector<CKDWORD>().swap(section.payload);
        std::vector<int>().swap(section.ids);
        std::vector<int>().swap(section.chunks);
        std::vector<int>().swap(section.managers);
    }
}

std::vector<CKDWORD> InterfaceChunkSections::UnparsedIdentifiers() const {
    std::vector<CKDWORD> result;
    for (const auto &section : sections)
        if (!known.count(section.identifier)) result.push_back(section.identifier);
    return result;
}

bool InterfaceChunkSections::RemoveUnparsed(CKDWORD identifier) {
    if (known.count(identifier)) return false;
    const auto found = std::find_if(sections.begin(), sections.end(), [&](const Section &section) {
        return section.identifier == identifier;
    });
    if (found == sections.end()) return false;
    sections.erase(found);
    return true;
}

bool InterfaceChunkSections::SplitList(const std::vector<CKDWORD> &data,
                                      const std::vector<int> &list, CHUNK_OPTIONS kind,
                                      std::vector<Section> &sections) {
    for (size_t i = 0; i < list.size(); ++i) {
        const bool sequence = list[i] == -1;
        if (sequence && ++i == list.size()) return false;
        const int position = list[i];
        if (position < 0 || static_cast<size_t>(position) >= data.size()) return false;
        size_t offset = 0;
        Section *owner = nullptr;
        for (auto &section : sections) {
            const size_t end = offset + 2 + section.payload.size();
            if (static_cast<size_t>(position) >= offset + 2 && static_cast<size_t>(position) < end) {
                owner = &section;
                break;
            }
            offset = end;
        }
        if (!owner) return false;
        const size_t local = static_cast<size_t>(position) - offset - 2;
        size_t count = 1, cursor = local;
        if (sequence) {
            count = owner->payload[cursor++];
            if (count > owner->payload.size()) return false;
        }
        const bool fits = kind == CHNK_OPTION_IDS ? Fits(owner->payload, cursor, count) :
            kind == CHNK_OPTION_CHN ? SubChunksFit(owner->payload, cursor, count) :
            Fits(owner->payload, cursor, sequence ? count + 2 : 3);
        if (!fits) return false;
        auto &target = kind == CHNK_OPTION_IDS ? owner->ids : kind == CHNK_OPTION_CHN ? owner->chunks : owner->managers;
        if (sequence) target.push_back(-1);
        target.push_back(static_cast<int>(local));
    }
    return true;
}

CKERROR InterfaceChunkSections::Encode(CKStateChunk *chunk,
                                       const std::vector<const Section *> &ordered) const {
    std::vector<CKDWORD> data;
    std::vector<int> ids, chunks, managers;
    auto appendList = [](std::vector<int> &target, const std::vector<int> &source, int offset) {
        for (int position : source) target.push_back(position < 0 ? position : position + offset);
    };
    for (size_t i = 0; i < ordered.size(); ++i) {
        const Section &section = *ordered[i];
        const size_t start = data.size();
        if (section.payload.size() > static_cast<size_t>((std::numeric_limits<int>::max)() / 4) - 2 ||
            start > static_cast<size_t>((std::numeric_limits<int>::max)() / 4) - 2 - section.payload.size())
            return CKERR_INVALIDPARAMETER;
        data.push_back(section.identifier);
        data.push_back(0);
        data.insert(data.end(), section.payload.begin(), section.payload.end());
        if (i + 1 < ordered.size()) data[start + 1] = static_cast<CKDWORD>(data.size());
        const int offset = static_cast<int>(start + 2);
        appendList(ids, section.ids, offset);
        appendList(chunks, section.chunks, offset);
        appendList(managers, section.managers, offset);
    }
    CKDWORD options = (metadata >> 24) & CHNK_OPTION_ALLOWDYN;
    if (!ids.empty()) options |= CHNK_OPTION_IDS;
    if (!chunks.empty()) options |= CHNK_OPTION_CHN;
    if (!managers.empty()) options |= CHNK_OPTION_MAN;
    std::vector<CKDWORD> buffer = {(metadata & 0x00FFFFFF) | (options << 24), static_cast<CKDWORD>(data.size())};
    buffer.insert(buffer.end(), data.begin(), data.end());
    auto writeList = [&](const std::vector<int> &list) {
        if (list.empty()) return;
        buffer.push_back(static_cast<CKDWORD>(list.size()));
        for (int position : list) buffer.push_back(static_cast<CKDWORD>(position));
    };
    writeList(ids); writeList(chunks); writeList(managers);
    if (buffer.size() > static_cast<size_t>((std::numeric_limits<int>::max)() / 4))
        return CKERR_INVALIDPARAMETER;
    return chunk->ConvertFromBuffer(buffer.data()) ? CK_OK : CKERR_INVALIDPARAMETER;
}

CKERROR InterfaceChunkSections::Merge(CKStateChunk *chunk, CKDWORD version, bool nonScript) const {
    const bool hasUnknown = std::any_of(sections.begin(), sections.end(), [&](const Section &section) {
        return known.count(section.identifier) == 0;
    });
    if (hasUnknown && ((metadata >> 24) & CHNK_OPTION_FILE)) return CKERR_NOTIMPLEMENTED;
    if (formatVersion != version || rootIsNotScript != nonScript)
        return hasUnknown ? CKERR_NOTIMPLEMENTED : CK_OK;
    std::shared_ptr<InterfaceChunkSections> generated;
    const CKERROR error = Read(chunk, generated);
    if (error != CK_OK) return error;
    // A CKFile-associated writer has already converted known ObjectIDs into
    // file indices. Without unparsed sections, keep that SDK output as-is;
    // otherwise merging in-memory references would mix incompatible forms.
    if ((generated->metadata >> 24) & CHNK_OPTION_FILE)
        return hasUnknown ? CKERR_NOTIMPLEMENTED : CK_OK;
    std::vector<const Section *> ordered;
    std::set<CKDWORD> used;
    for (const auto &original : sections) {
        const auto replacement = std::find_if(generated->sections.begin(), generated->sections.end(),
            [&](const Section &candidate) { return candidate.identifier == original.identifier; });
        if (!known.count(original.identifier)) {
            // A previously unparsed identifier cannot also carry a newly
            // generated definition. Do not silently choose either payload.
            if (replacement != generated->sections.end()) return CKERR_NOTIMPLEMENTED;
            ordered.push_back(&original);
        } else if (replacement != generated->sections.end()) {
            ordered.push_back(&*replacement);
        }
        used.insert(original.identifier);
    }
    for (const auto &section : generated->sections)
        if (!used.count(section.identifier)) ordered.push_back(&section);
    return Encode(chunk, ordered);
}
