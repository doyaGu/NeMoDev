#pragma once

#include <string>
#include <vector>

#include "CKDefines.h"
#include "CKStateChunk.h"

/**
 * @enum LinkType
 * @brief Defines the types of links in behavior networks
 *
 * Inline chunks (0x12..0x16) pack type/highlight into a DWORD.
 * Older sectioned chunks store highlight and type as separate DWORDs.
 */
enum LinkType {
    LINK_TYPE_BEHAVIOR     = 1,      ///< Behavior link (control flow)
    LINK_TYPE_PARAMETER    = 2,      ///< Parameter link (data flow)
};

/**
 * @brief Highlight mode flag for links
 *
 * Inline chunks set this bit in the stored link type value when the
 * link is displayed in highlight mode in the Virtools schematic editor.
 */
constexpr CKDWORD LINK_FLAG_HIGHLIGHT = 0x10000;

/**
 * @brief Mask to extract the actual link type from the stored value
 */
constexpr CKDWORD LINK_TYPE_MASK = 0xFFFF;

CKDWORD InterfaceDataSectionId(CKDWORD behaviorIndex, CKDWORD baseId);
CKDWORD EncodeLegacyInlineLinkType(LinkType type, bool highlight);
void DecodeLegacyInlineLinkType(CKDWORD rawType, LinkType& type, bool& highlight);
void DecodeDevSectionedLinkType(CKDWORD highlightValue, CKDWORD typeValue, LinkType& type, bool& highlight);

namespace InterfaceChunkFormat {

constexpr CKDWORD LEGACY_TOP_LEVEL_MARKER = 1;
constexpr CKDWORD DEV_TOP_LEVEL_MARKER = 0xB0000001;
constexpr CKDWORD DEV_SECTION_HEADER = 0xB0010000;
constexpr CKDWORD DEV_SECTION_OPERATIONS = 0xB0020000;
constexpr CKDWORD DEV_SECTION_LINKS = 0xB0030000;
constexpr CKDWORD DEV_SECTION_COMMENTS = 0xB0080000;
constexpr CKDWORD DEV_SECTION_UNKNOWN_FLAG = 0xB00A0000;
constexpr CKDWORD DEV_SECTION_SCRIPT_HEADER = 0xB0070000;
constexpr CKDWORD DEV_SECTION_SCRIPT_MARKER = 0xB0000002;
constexpr CKDWORD DEV_SECTION_GRAPH_MARKER = 0xB0000000;
constexpr CKDWORD EXTRA_DATA_V1_MARKER = 0xA12312F5;
constexpr CKDWORD EXTRA_DATA_V2_MARKER = 0xA12312F6;
constexpr CKDWORD EXTRA_DATA_V3_MARKER = 0xA12312F7;

constexpr CKDWORD MIN_SUPPORTED_VERSION = 5;
// Historical public name: IO here means input/output PARAMETER display tables.
constexpr CKDWORD SECTIONED_FOLDED_IO_MIN_VERSION = 6;
constexpr CKDWORD SECTIONED_START_POSITION_MIN_VERSION = 7;
constexpr CKDWORD SECTIONED_PARAMETER_STYLE_MIN_VERSION = 8;
constexpr CKDWORD SECTIONED_SHARED_PARAMETERS_MIN_VERSION = 10;
constexpr CKDWORD PLAIN_SECTION_MIN_VERSION = 0x0D;
constexpr CKDWORD INLINE_MIN_VERSION = 0x12;
constexpr CKDWORD SECTIONED_STYLE_FLAGS_MIN_VERSION = 0x10;
constexpr CKDWORD DIRECT_ENDPOINT_TYPE_MIN_VERSION = 0x11;
constexpr CKDWORD SECTIONED_TAGGED_SOURCE_MIN_VERSION = 0x0E;
constexpr CKDWORD SECTIONED_SCRIPT_HEIGHT_MIN_VERSION = 0x0F;
constexpr CKDWORD SECTION_LOCAL_PARAMETERS = 0x04000000;
constexpr CKDWORD SECTION_SHARED_PARAMETERS = 0x09000000;
constexpr CKDWORD SECTION_INPUT_POSITIONS = 0x05000000;
constexpr CKDWORD SECTION_OUTPUT_POSITIONS = 0x06000000;
constexpr CKDWORD SECTION_SCRIPT_FLAG = 0x0A000000;
constexpr CKDWORD LEGACY_GRAPH_TABLE_VERSION = 0x12;
constexpr CKDWORD HEADER_COLOR_MIN_VERSION = 0x14;
constexpr CKDWORD PARAMETER_SOURCE_ID_MIN_VERSION = 0x15;
constexpr CKDWORD COMMENT_STYLE_MIN_VERSION = 0x16;
constexpr CKDWORD CURRENT_VERSION = 0x16;
constexpr CKDWORD MAX_SUPPORTED_VERSION = CURRENT_VERSION;
constexpr CKDWORD BEHAVIOR_FLAG_FOLDED = 0x200;
constexpr CKDWORD BEHAVIOR_FLAG_BODY_OMITTED = 0x8000;
constexpr CKDWORD BEHAVIOR_FLAG_SCRIPT_HIDDEN = 0x20000;
constexpr int EXTRA_DATA_SUBDATA_MIN_VERSION = 2;
constexpr int EXTRA_SUBDATA_V2_DECODE_THRESHOLD = 4;
constexpr int EXTRA_SUBDATA_V2_ENCODE_THRESHOLD = 6;
constexpr int DEV_SECTION_UNKNOWN_FLAG_VALUE = 0;
constexpr int GRAPH_INWARD_EXTRA_VALUE = -1;
constexpr int GRAPH_OUTWARD_EXTRA_VALUE = 1;
constexpr CK_ID LEGACY_SHARED_PARAMETER_SOURCE_PADDING_ID = 0;
constexpr int LEGACY_SHARED_PARAMETER_SOURCE_PADDING_VALUE = 0;
constexpr CKDWORD DEFAULT_HEADER_COLOR = 0xC8C8C8;

CKDWORD SectionId(CKDWORD behaviorIndex, CKDWORD baseId);
bool VersionUsesSections(CKDWORD version);
CKDWORD SectionedIdentifier(CKDWORD version, CKDWORD index, CKDWORD plainBase);
CKDWORD SectionedKindIdentifier(CKDWORD version, bool isNotScript);
CKDWORD SectionedHeaderIdentifier(CKDWORD version, CKDWORD index, bool isNotScript);
int DecodeParameterStyle(CKDWORD version, int rawStyle);
int EncodeParameterStyle(CKDWORD version, int style);
// Historical marker conveniences, retained for source compatibility. Their
// bool selects B identifiers, NOT the payload layout. Layout comes from version.
CKDWORD TopLevelMarkerForLayout(bool useBIdentifier);
CKDWORD DevScriptKindMarker(bool isNotScript);
CKDWORD DevHeaderSectionId(CKDWORD layoutIndex, bool isNotScript);
CKDWORD DevLinksSectionId(CKDWORD layoutIndex);
CKDWORD DevOperationsSectionId(CKDWORD layoutIndex);
CKDWORD DevCommentsSectionId(CKDWORD layoutIndex);
CKDWORD DevUnknownFlagSectionId(CKDWORD layoutIndex);
CKBOOL DecodeTopLevelMarker(CKDWORD marker, bool &usesBIdentifier);
CKBOOL DecodeDevScriptKindMarker(CKDWORD marker, bool &isNotScript);
int ExtraDataVersionFromMarker(CKDWORD marker);
CKDWORD ExtraDataMarkerForVersion(int version);
bool ExtraDataVersionHasSubData(int version);
int DecodeExtraSubDataValue(int version, int rawValue);
int EncodeExtraSubDataValue(int version, int normalizedValue);
bool ExtraSubDataValueStoresObjectId(int value);

CKDWORD EncodeLegacyInlineLinkType(LinkType type, bool highlight);
void DecodeLegacyInlineLinkType(CKDWORD rawType, LinkType& type, bool& highlight);
void DecodeDevSectionedLinkType(CKDWORD highlightValue, CKDWORD typeValue, LinkType& type, bool& highlight);

// Low-level SDK helpers retained for callers with validated input. Full
// InterfaceChunk reads with section bounds use InterfaceData::LoadFromChunk.
CKBOOL TrySeekIdentifier(CKStateChunk *chunk, CKDWORD identifier);
CKBOOL TryReadTopLevelVersion(CKStateChunk *chunk, CKDWORD &version);
CKBOOL TryReadTopLevelVersion(CKStateChunk *chunk, CKDWORD &version, CKDWORD &identifier);
CKBOOL TryReadDevScriptKindMarker(CKStateChunk *chunk, bool &isNotScript);
void ReadStringDwords(CKStateChunk *chunk, std::string &text, std::vector<CKDWORD> &serialized);
void WriteStringDwords(CKStateChunk *chunk, const std::string &text, const std::vector<CKDWORD> &serialized);
void ReadBufferDwords(CKStateChunk *chunk, std::vector<CKBYTE> &buffer, std::vector<CKDWORD> &serialized);
// Validated effective byte length must fit nonnegative int32. Reuse an exact
// matching original encoding; otherwise write the bytes with zero DWORD padding.
void WriteBufferDwords(CKStateChunk *chunk, const std::vector<CKBYTE> &buffer, const std::vector<CKDWORD> &serialized);

} // namespace InterfaceChunkFormat
