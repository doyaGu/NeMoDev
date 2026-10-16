#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CKStateChunk.h"

class InterfaceBitmap;
class InterfaceChunkSections;

// Internal parse failure; translated to CKERROR at LoadFromChunk only.
struct InterfaceChunkReadError {
    CKERROR code;
    CKDWORD identifier;
};

// Reads the original SDK chunk (including its CKFile association), bounded by
// the validated identifier chain. All offsets and limits here are DWORDs.
class InterfaceChunkReader {
public:
    InterfaceChunkReader(CKStateChunk *chunk, const InterfaceChunkSections &sections,
                         int &diagnosticOffset);
    ~InterfaceChunkReader();
    InterfaceChunkReader(const InterfaceChunkReader &) = delete;
    InterfaceChunkReader &operator=(const InterfaceChunkReader &) = delete;

    bool SeekIdentifier(CKDWORD identifier);
    void FinishSection();
    int GetCurrentPos() const;
    int ReadInt();
    CKDWORD ReadDword();
    float ReadFloat();
    CK_ID ReadObjectID();
    int ReadCount(int minimumRecordWords, int trailingWords = 0);
    void ReadStringDwords(std::string &text, std::vector<CKDWORD> &serialized);
    void ReadBuffer(std::vector<CKBYTE> &buffer, std::vector<CKDWORD> &serialized);
    std::shared_ptr<InterfaceBitmap> ReadBitmap();

private:
    void Require(uint64_t words);
    uint64_t BufferWords(int lengthWordOffset);
    CKStateChunk *chunk;
    const InterfaceChunkSections &sections;
    int &diagnosticOffset;
    CKDWORD identifier = 0;
    int begin = -1;
    int end = -1;
};
