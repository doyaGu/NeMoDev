#include "core/interface/detail/InterfaceChunkReader.h"

#include "core/interface/detail/InterfaceBitmap.h"
#include "core/interface/InterfaceChunkFormat.h"
#include "core/interface/detail/InterfaceChunkSections.h"

InterfaceChunkReader::InterfaceChunkReader(CKStateChunk *input,
        const InterfaceChunkSections &storage, int &offset)
    : chunk(input), sections(storage), diagnosticOffset(offset) {
    chunk->StartRead();
}

InterfaceChunkReader::~InterfaceChunkReader() {
    chunk->CloseChunk();
}

int InterfaceChunkReader::GetCurrentPos() const {
    return chunk->GetCurrentPos();
}

void InterfaceChunkReader::Require(uint64_t words) {
    const int position = GetCurrentPos();
    diagnosticOffset = position;
    if (begin < 0 || position < begin || position > end ||
        words > static_cast<uint64_t>(end - position))
        throw InterfaceChunkReadError{CKERR_INVALIDPARAMETER, identifier};
}

void InterfaceChunkReader::FinishSection() {
    if (begin < 0) return;
    Require(0);
    if (GetCurrentPos() != end)
        throw InterfaceChunkReadError{CKERR_NOTIMPLEMENTED, identifier};
}

bool InterfaceChunkReader::SeekIdentifier(CKDWORD nextIdentifier) {
    int nextBegin, nextEnd;
    if (!sections.PayloadBounds(nextIdentifier, nextBegin, nextEnd)) return false;
    FinishSection();
    identifier = nextIdentifier;
    if (!chunk->SeekIdentifier(identifier) || GetCurrentPos() != nextBegin)
        throw InterfaceChunkReadError{CKERR_INVALIDPARAMETER, identifier};
    begin = nextBegin;
    end = nextEnd;
    diagnosticOffset = begin;
    return true;
}

int InterfaceChunkReader::ReadInt() {
    Require(1);
    return chunk->ReadInt();
}

CKDWORD InterfaceChunkReader::ReadDword() {
    Require(1);
    return chunk->ReadDword();
}

float InterfaceChunkReader::ReadFloat() {
    Require(1);
    return chunk->ReadFloat();
}

CK_ID InterfaceChunkReader::ReadObjectID() {
    // InterfaceChunkSections accepts SDK container versions 6/7 only. Their
    // ObjectID occupies one DWORD; the SDK resolves CKFile indices if present.
    Require(1);
    return chunk->ReadObjectID();
}

int InterfaceChunkReader::ReadCount(int minimumRecordWords, int trailingWords) {
    const int count = ReadInt();
    const int available = end - GetCurrentPos();
    if (count < 0 || minimumRecordWords <= 0 || trailingWords < 0 ||
        available < trailingWords || count > (available - trailingWords) / minimumRecordWords)
        throw InterfaceChunkReadError{CKERR_INVALIDPARAMETER, identifier};
    return count;
}

uint64_t InterfaceChunkReader::BufferWords(int lengthWordOffset) {
    Require(lengthWordOffset + 1);
    const auto data = static_cast<const CKDWORD *>(chunk->LockReadBuffer());
    if (!data) throw InterfaceChunkReadError{CKERR_INVALIDPARAMETER, identifier};
    const int length = static_cast<int>(data[lengthWordOffset]);
    if (length < 0) throw InterfaceChunkReadError{CKERR_INVALIDPARAMETER, identifier};
    const uint64_t words = lengthWordOffset + 1 + (static_cast<uint64_t>(length) + 3) / 4;
    Require(words);
    return words;
}

void InterfaceChunkReader::ReadStringDwords(std::string &text,
                                           std::vector<CKDWORD> &serialized) {
    BufferWords(0);
    InterfaceChunkFormat::ReadStringDwords(chunk, text, serialized);
}

void InterfaceChunkReader::ReadBuffer(std::vector<CKBYTE> &buffer,
                                      std::vector<CKDWORD> &serialized) {
    BufferWords(0);
    InterfaceChunkFormat::ReadBufferDwords(chunk, buffer, serialized);
}

std::shared_ptr<InterfaceBitmap> InterfaceChunkReader::ReadBitmap() {
    BufferWords(1);
    auto bitmap = InterfaceBitmap::Read(chunk);
    if (!bitmap) throw InterfaceChunkReadError{CKERR_INVALIDPARAMETER, identifier};
    return bitmap;
}
