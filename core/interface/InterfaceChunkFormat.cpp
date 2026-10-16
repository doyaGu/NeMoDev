#include "core/interface/InterfaceChunkFormat.h"

#include <cstdint>
#include <cstring>

CKDWORD InterfaceDataSectionId(CKDWORD behaviorIndex, CKDWORD baseId) {
    return InterfaceChunkFormat::SectionId(behaviorIndex, baseId);
}

CKDWORD EncodeLegacyInlineLinkType(LinkType type, bool highlight) {
    return InterfaceChunkFormat::EncodeLegacyInlineLinkType(type, highlight);
}

void DecodeLegacyInlineLinkType(CKDWORD rawType, LinkType& type, bool& highlight) {
    InterfaceChunkFormat::DecodeLegacyInlineLinkType(rawType, type, highlight);
}

void DecodeDevSectionedLinkType(CKDWORD highlightValue, CKDWORD typeValue, LinkType& type, bool& highlight) {
    InterfaceChunkFormat::DecodeDevSectionedLinkType(highlightValue, typeValue, type, highlight);
}

namespace InterfaceChunkFormat {

CKDWORD SectionId(CKDWORD behaviorIndex, CKDWORD baseId) {
    return baseId + behaviorIndex;
}

bool VersionUsesSections(CKDWORD version) { return version < INLINE_MIN_VERSION; }

CKDWORD SectionedIdentifier(CKDWORD version, CKDWORD index, CKDWORD plainBase) {
    const CKDWORD base = version < PLAIN_SECTION_MIN_VERSION
        ? (0xB0000000 | (plainBase >> 8)) : plainBase;
    return base + index;
}

CKDWORD SectionedKindIdentifier(CKDWORD version, bool isNotScript) {
    if (version < PLAIN_SECTION_MIN_VERSION)
        return DevScriptKindMarker(isNotScript);
    return isNotScript ? 3 : 2;
}

CKDWORD SectionedHeaderIdentifier(CKDWORD version, CKDWORD index, bool isNotScript) {
    return SectionedIdentifier(version, isNotScript ? index : 0,
                               isNotScript ? 0x01000000 : 0x07000000);
}

int DecodeParameterStyle(CKDWORD version, int rawStyle) {
    if (version >= PLAIN_SECTION_MIN_VERSION && version < SECTIONED_STYLE_FLAGS_MIN_VERSION &&
        rawStyle >= 0 && rawStyle <= 4)
        return 0x200 << rawStyle;
    return rawStyle;
}

int EncodeParameterStyle(CKDWORD version, int style) {
    if (version >= PLAIN_SECTION_MIN_VERSION && version < SECTIONED_STYLE_FLAGS_MIN_VERSION) {
        for (int i = 0; i <= 4; ++i)
            if (style == (0x200 << i)) return i;
    }
    return style;
}

CKDWORD TopLevelMarkerForLayout(bool useBIdentifier) {
    return useBIdentifier ? DEV_TOP_LEVEL_MARKER : LEGACY_TOP_LEVEL_MARKER;
}

CKDWORD DevScriptKindMarker(bool isNotScript) {
    return isNotScript ? DEV_SECTION_GRAPH_MARKER : DEV_SECTION_SCRIPT_MARKER;
}

CKDWORD DevHeaderSectionId(CKDWORD layoutIndex, bool isNotScript) {
    return isNotScript ? SectionId(layoutIndex, DEV_SECTION_HEADER) : DEV_SECTION_SCRIPT_HEADER;
}

CKDWORD DevLinksSectionId(CKDWORD layoutIndex) {
    return SectionId(layoutIndex, DEV_SECTION_LINKS);
}

CKDWORD DevOperationsSectionId(CKDWORD layoutIndex) {
    return SectionId(layoutIndex, DEV_SECTION_OPERATIONS);
}

CKDWORD DevCommentsSectionId(CKDWORD layoutIndex) {
    return SectionId(layoutIndex, DEV_SECTION_COMMENTS);
}

CKDWORD DevUnknownFlagSectionId(CKDWORD layoutIndex) {
    return SectionId(layoutIndex, DEV_SECTION_UNKNOWN_FLAG);
}

CKBOOL DecodeTopLevelMarker(CKDWORD marker, bool &usesBIdentifier) {
    if (marker == DEV_TOP_LEVEL_MARKER) {
        usesBIdentifier = true;
        return TRUE;
    }

    if (marker == LEGACY_TOP_LEVEL_MARKER) {
        usesBIdentifier = false;
        return TRUE;
    }

    return FALSE;
}

CKBOOL DecodeDevScriptKindMarker(CKDWORD marker, bool &isNotScript) {
    if (marker == DEV_SECTION_SCRIPT_MARKER) {
        isNotScript = false;
        return TRUE;
    }

    if (marker == DEV_SECTION_GRAPH_MARKER) {
        isNotScript = true;
        return TRUE;
    }

    return FALSE;
}

int ExtraDataVersionFromMarker(CKDWORD marker) {
    switch (marker) {
    case EXTRA_DATA_V3_MARKER:
        return 3;
    case EXTRA_DATA_V2_MARKER:
        return 2;
    case EXTRA_DATA_V1_MARKER:
        return 1;
    default:
        return 0;
    }
}

CKDWORD ExtraDataMarkerForVersion(int version) {
    switch (version) {
    case 3:
        return EXTRA_DATA_V3_MARKER;
    case 2:
        return EXTRA_DATA_V2_MARKER;
    case 1:
    default:
        return EXTRA_DATA_V1_MARKER;
    }
}

bool ExtraDataVersionHasSubData(int version) {
    return version >= EXTRA_DATA_SUBDATA_MIN_VERSION;
}

int DecodeExtraSubDataValue(int version, int rawValue) {
    if (version == EXTRA_DATA_SUBDATA_MIN_VERSION && rawValue >= EXTRA_SUBDATA_V2_DECODE_THRESHOLD) {
        return rawValue + 2;
    }
    return rawValue;
}

int EncodeExtraSubDataValue(int version, int normalizedValue) {
    if (version == EXTRA_DATA_SUBDATA_MIN_VERSION && normalizedValue >= EXTRA_SUBDATA_V2_ENCODE_THRESHOLD) {
        return normalizedValue - 2;
    }
    return normalizedValue;
}

bool ExtraSubDataValueStoresObjectId(int value) {
    switch (value) {
    case 2:
    case 3:
    case 8:
    case 9:
    case 10:
    case 11:
        return true;
    default:
        return false;
    }
}

CKDWORD EncodeLegacyInlineLinkType(LinkType type, bool highlight) {
    CKDWORD rawType = static_cast<CKDWORD>(type) & LINK_TYPE_MASK;
    if (highlight) {
        rawType |= LINK_FLAG_HIGHLIGHT;
    }
    return rawType;
}

void DecodeLegacyInlineLinkType(CKDWORD rawType, LinkType& type, bool& highlight) {
    type = static_cast<LinkType>(rawType & LINK_TYPE_MASK);
    highlight = (rawType & LINK_FLAG_HIGHLIGHT) != 0;
}

void DecodeDevSectionedLinkType(CKDWORD highlightValue, CKDWORD typeValue, LinkType& type, bool& highlight) {
    type = static_cast<LinkType>(typeValue & LINK_TYPE_MASK);
    highlight = highlightValue != 0;
}

CKBOOL TrySeekIdentifier(CKStateChunk *chunk, CKDWORD identifier) {
    if (!chunk) {
        return FALSE;
    }

    const int savedPos = chunk->GetCurrentPos();
    if (chunk->SeekIdentifier(identifier)) {
        return TRUE;
    }

    chunk->Goto(savedPos);
    return FALSE;
}

CKBOOL TryReadTopLevelVersion(CKStateChunk *chunk, CKDWORD &version) {
    CKDWORD identifier = 0;
    return TryReadTopLevelVersion(chunk, version, identifier);
}

CKBOOL TryReadTopLevelVersion(CKStateChunk *chunk, CKDWORD &version, CKDWORD &identifier) {
    // Dev tries B0000001 first, then 1; when both exist, 1 takes precedence.
    const CKDWORD markers[] = {LEGACY_TOP_LEVEL_MARKER, DEV_TOP_LEVEL_MARKER};
    for (CKDWORD marker : markers) {
        bool usesBIdentifier = false;
        if (DecodeTopLevelMarker(marker, usesBIdentifier) &&
            TrySeekIdentifier(chunk, marker)) {
            version = chunk->ReadInt();
            identifier = marker;
            return TRUE;
        }
    }

    return FALSE;
}

CKBOOL TryReadDevScriptKindMarker(CKStateChunk *chunk, bool &isNotScript) {
    const CKDWORD markers[] = {DEV_SECTION_SCRIPT_MARKER, DEV_SECTION_GRAPH_MARKER};
    for (CKDWORD marker : markers) {
        bool decodedIsNotScript = false;
        if (DecodeDevScriptKindMarker(marker, decodedIsNotScript) &&
            TrySeekIdentifier(chunk, marker)) {
            isNotScript = decodedIsNotScript;
            return TRUE;
        }
    }

    return FALSE;
}

namespace {
int ReadSizePrefixedDwords(CKStateChunk *chunk, std::vector<CKDWORD> &serialized) {
    serialized.clear();

    const int size = chunk->ReadInt();
    serialized.push_back(static_cast<CKDWORD>(size));
    if (size <= 0) {
        return size;
    }

    const int payloadDwords = static_cast<int>((static_cast<uint64_t>(size) + sizeof(CKDWORD) - 1) / sizeof(CKDWORD));
    serialized.reserve(1 + payloadDwords);
    for (int i = 0; i < payloadDwords; ++i) {
        serialized.push_back(chunk->ReadDword());
    }
    return size;
}

void WriteDwords(CKStateChunk *chunk, const std::vector<CKDWORD> &serialized) {
    for (CKDWORD value : serialized) chunk->WriteDword(value);
}
} // namespace

void ReadStringDwords(CKStateChunk *chunk, std::string &text, std::vector<CKDWORD> &serialized) {
    text.clear();
    const int size = ReadSizePrefixedDwords(chunk, serialized);
    if (size <= 0) return;

    const char *chars = reinterpret_cast<const char *>(serialized.data() + 1);
    size_t length = 0;
    while (length < static_cast<size_t>(size) && chars[length] != '\0') {
        ++length;
    }
    text.assign(chars, length);
}

void WriteStringDwords(CKStateChunk *chunk, const std::string &text, const std::vector<CKDWORD> &serialized) {
    if (!serialized.empty()) {
        const size_t size = serialized[0];
        const size_t capacity = (serialized.size() - 1) * sizeof(CKDWORD);
        const char *chars = reinterpret_cast<const char *>(serialized.data() + 1);
        size_t length = 0;
        while (length < size && length < capacity && chars[length] != '\0') ++length;
        if (size <= capacity && text == std::string(chars, length)) {
            WriteDwords(chunk, serialized);
            return;
        }
    }

    std::vector<char> writableText(text.begin(), text.end());
    writableText.push_back('\0');
    chunk->WriteString(writableText.data());
}

void ReadBufferDwords(CKStateChunk *chunk, std::vector<CKBYTE> &buffer,
                      std::vector<CKDWORD> &serialized) {
    buffer.clear();
    const int size = ReadSizePrefixedDwords(chunk, serialized);
    if (size <= 0) return;
    const auto bytes = reinterpret_cast<const CKBYTE *>(serialized.data() + 1);
    buffer.assign(bytes, bytes + size);
}

void WriteBufferDwords(CKStateChunk *chunk, const std::vector<CKBYTE> &buffer,
                       const std::vector<CKDWORD> &serialized) {
    const size_t size = buffer.size();
    const size_t payloadWords = size / sizeof(CKDWORD) + (size % sizeof(CKDWORD) != 0);
    if (serialized.size() == 1 + payloadWords && serialized[0] == size &&
        (!size || std::memcmp(serialized.data() + 1, buffer.data(), size) == 0)) {
        WriteDwords(chunk, serialized);
        return;
    }
    // New/edited values have explicit zero padding. Padding is not part of the
    // comparison value and is never registered as an ObjectID or a sequence.
    chunk->WriteInt(static_cast<int>(size));
    for (size_t offset = 0; offset < size; offset += sizeof(CKDWORD)) {
        CKDWORD word = 0;
        const size_t remaining = size - offset;
        std::memcpy(&word, buffer.data() + offset,
                    remaining < sizeof(CKDWORD) ? remaining : sizeof(CKDWORD));
        chunk->WriteDword(word);
    }
}

} // namespace InterfaceChunkFormat
