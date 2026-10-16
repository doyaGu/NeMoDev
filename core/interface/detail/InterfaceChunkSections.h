#pragma once

#include <memory>
#include <set>
#include <vector>

#include "CKStateChunk.h"

// Internal CKStateChunk section storage. Payload and reference-position lists
// are kept together; unknown payload is never interpreted as InterfaceData.
class InterfaceChunkSections {
public:
    static CKERROR Read(CKStateChunk *chunk, std::shared_ptr<InterfaceChunkSections> &result);
    bool PayloadBounds(CKDWORD identifier, int &begin, int &end) const;
    void MarkKnown(CKDWORD identifier) { known.insert(identifier); }
    void FinishRead(CKDWORD version, bool nonScript);
    CKERROR Merge(CKStateChunk *chunk, CKDWORD version, bool nonScript) const;
    std::vector<CKDWORD> UnparsedIdentifiers() const;
    bool RemoveUnparsed(CKDWORD identifier);

private:
    struct Section {
        CKDWORD identifier = 0;
        int begin = 0;
        int end = 0;
        std::vector<CKDWORD> payload;
        std::vector<int> ids;
        std::vector<int> chunks;
        std::vector<int> managers;
    };
    CKERROR Decode(const std::vector<CKDWORD> &buffer);
    CKERROR Encode(CKStateChunk *chunk, const std::vector<const Section *> &ordered) const;
    static bool SplitList(const std::vector<CKDWORD> &data, const std::vector<int> &list,
                          CHUNK_OPTIONS kind, std::vector<Section> &sections);

    CKDWORD metadata = 0;
    CKDWORD formatVersion = 0;
    bool rootIsNotScript = false;
    std::vector<Section> sections;
    std::set<CKDWORD> known;
};
