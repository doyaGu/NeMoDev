#include "core/interface/InterfaceData.h"
#include "core/interface/InterfaceDataValidation.h"
#include "core/interface/InterfaceChunkFormat.h"
#include "CKGlobals.h"
#include "core/interface/detail/InterfaceBitmap.h"
#include "core/interface/detail/InterfaceChunkSections.h"
#include "core/interface/detail/InterfaceChunkReader.h"

#include <sstream>
#include <limits>

namespace Format = InterfaceChunkFormat;
namespace {
bool ScriptStartPositionIsStored(CKDWORD version, bool isNotScript) {
    return !isNotScript && (!Format::VersionUsesSections(version) ||
                           version >= Format::SECTIONED_START_POSITION_MIN_VERSION);
}

bool ScriptHeightIsStored(CKDWORD version, bool isNotScript) {
    return !isNotScript && (!Format::VersionUsesSections(version) ||
                           version >= Format::SECTIONED_SCRIPT_HEIGHT_MIN_VERSION);
}

bool ScriptColorIsStored(CKDWORD version, bool isNotScript) {
    return !isNotScript && !Format::VersionUsesSections(version) && version >= Format::HEADER_COLOR_MIN_VERSION;
}

bool BehaviorSnapshotIsStored(CKDWORD version, bool isNotScript) {
    return Format::VersionUsesSections(version) && isNotScript;
}

bool CommentStyleIsStored(CKDWORD version) {
    return !Format::VersionUsesSections(version) && version >= Format::COMMENT_STYLE_MIN_VERSION;
}

bool ScriptIndexIsStored(CKDWORD version, bool isNotScript) {
    return !Format::VersionUsesSections(version) && !isNotScript;
}

bool BehaviorDepthIsStored(CKDWORD version, bool isNotScript) {
    return !Format::VersionUsesSections(version) && isNotScript;
}

CKDWORD BehaviorFlagsForSave(const BehaviorData &behavior) {
    return (behavior.flags & ~Format::BEHAVIOR_FLAG_FOLDED) |
           (behavior.folded ? Format::BEHAVIOR_FLAG_FOLDED : 0);
}

CKDWORD SectionedBehaviorFlags(CKDWORD version, bool isNotScript, bool folded, int scriptFlag) {
    CKDWORD flags = folded ? Format::BEHAVIOR_FLAG_FOLDED : 0;
    if (scriptFlag == 1) {
        if (!isNotScript) flags |= Format::BEHAVIOR_FLAG_SCRIPT_HIDDEN;
        else if (version == 17) flags |= Format::BEHAVIOR_FLAG_BODY_OMITTED;
    }
    return flags;
}

CKERROR CheckScriptHeaderForSave(CKDWORD version, bool isNotScript, const Header &header) {
    const Header defaults; // Library constructor defaults, not values recovered from an absent field.
    if (!ScriptIndexIsStored(version, isNotScript) && header.scriptIndex != defaults.scriptIndex)
        return CKERR_NOTIMPLEMENTED;
    if (!ScriptStartPositionIsStored(version, isNotScript) &&
        (header.hStartPos != defaults.hStartPos || header.vStartPos != defaults.vStartPos))
        return CKERR_NOTIMPLEMENTED;
    if (!ScriptHeightIsStored(version, isNotScript) && header.vSize != defaults.vSize)
        return CKERR_NOTIMPLEMENTED;
    if (!ScriptColorIsStored(version, isNotScript) && header.color != defaults.color)
        return CKERR_NOTIMPLEMENTED;
    return CK_OK;
}

CKERROR CheckBehaviorHeaderForSave(CKDWORD version, bool isNotScript, const BehaviorData &behavior) {
    if (!BehaviorDepthIsStored(version, isNotScript) && behavior.depth != 0)
        return CKERR_NOTIMPLEMENTED;
    if (Format::VersionUsesSections(version)) {
        // Old headers have no display-flags DWORD. Only these flags are derived
        // by Dev from state and ScriptFlag; never invent a mapping for the rest.
        if (BehaviorFlagsForSave(behavior) !=
            SectionedBehaviorFlags(version, isNotScript, behavior.folded, behavior.sectionedScriptFlag))
            return CKERR_NOTIMPLEMENTED;
    } else if (behavior.sectionedElementId != 0 || behavior.sectionedParentElementId != 0 ||
               behavior.sectionedState != 0 || behavior.sectionedScriptFlag != 0) {
        return CKERR_NOTIMPLEMENTED;
    }
    return CK_OK;
}
} // namespace

const BehaviorData &InterfaceData::GetBehaviorForContext(const SerializationContext &context) const {
    return context.layoutIndex == 0 ? rootBehavior : behaviors[context.behaviorIndex];
}

BehaviorData &InterfaceData::GetBehaviorForContext(SerializationContext &context) {
    return context.layoutIndex == 0 ? rootBehavior : behaviors[context.behaviorIndex];
}

CKERROR InterfaceData::LoadBehaviorHeader(SerializationContext &context, BehaviorData &behavior) {
    CKContext *ckContext = context.behavior->GetCKContext();
    InterfaceChunkReader *reader = context.reader;
    CKDWORD index = 0;
    CK_ID behID = 0;

    if (context.useDevSectionedLayout) {
        if (!reader->SeekIdentifier(Format::SectionedHeaderIdentifier(
                context.version, context.layoutIndex, context.isNotScript)))
            return CKERR_NOTFOUND;
        behavior.sectionedElementId = reader->ReadInt();
        behavior.sectionedParentElementId = reader->ReadInt();
        behID = reader->ReadObjectID();
        behavior.sectionedState = reader->ReadInt();
        behavior.folded = behavior.sectionedState != 0;
        behavior.flags = SectionedBehaviorFlags(context.version, context.isNotScript, behavior.folded, 0);
    } else {
        behID = reader->ReadObjectID();
        behavior.flags = reader->ReadDword();
        index = reader->ReadDword();
        behavior.folded = (behavior.flags & Format::BEHAVIOR_FLAG_FOLDED) != 0;
    }

    CKBehavior *beh = CKBehavior::Cast(ckContext->GetObject(behID));
    if (!beh) {
        ckContext->OutputToConsoleEx((CKSTRING)"Error: CKBehavior <%d> not found", behID);
        return CKERR_NOTFOUND;
    }
    context.flags = behavior.flags;
    context.behavior = beh;
    context.isBuildingBlock = (beh->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    behavior.id = behID;
    // Runtime ports/flags/Target are not fields in the InterfaceChunk.
    // Share this capture with GraphBuilder; never derive counts from links.
    behavior.UpdateRuntimeState(beh);
    behavior.rect.hPos = reader->ReadFloat();
    behavior.rect.vPos = reader->ReadFloat();

    if (!context.isNotScript) {
        if (ScriptIndexIsStored(context.version, context.isNotScript)) header.scriptIndex = index;
        context.scriptIndex = index;
        if (ScriptStartPositionIsStored(context.version, context.isNotScript)) {
            header.hStartPos = reader->ReadFloat();
            header.vStartPos = reader->ReadFloat();
        }
        if (ScriptHeightIsStored(context.version, context.isNotScript))
            header.vSize = reader->ReadFloat();
        header.snapshotStorage = reader->ReadBitmap();
        header.snapshot = header.snapshotStorage->GetHandle();
        if (ScriptColorIsStored(context.version, context.isNotScript))
            header.color = reader->ReadDword();
    } else {
        if (BehaviorDepthIsStored(context.version, context.isNotScript)) behavior.depth = index;
        behavior.rect.hSize = reader->ReadFloat();
        behavior.rect.vSize = reader->ReadFloat();
        behavior.hExpandSize = reader->ReadFloat();
        behavior.vExpandSize = reader->ReadFloat();
        if (BehaviorSnapshotIsStored(context.version, context.isNotScript)) {
            behavior.sectionedSnapshotStorage = reader->ReadBitmap();
            behavior.sectionedSnapshot = behavior.sectionedSnapshotStorage->GetHandle();
        }
    }
    return CK_OK;
}

void InterfaceData::LoadBehaviorLinks(SerializationContext &context, BehaviorData &behavior) {
    InterfaceChunkReader *reader = context.reader;

    // Read link count
    const int linkCount = reader->ReadCount(context.useDevSectionedLayout ? 10 : 9);
    const int linkCountOffset = reader->GetCurrentPos() - 1;
    lastLoadDwordOffset = linkCountOffset;
    {
        std::ostringstream phase;
        phase << "behavior[" << context.layoutIndex << "].links.count=" << linkCount;
        lastLoadPhase = phase.str();
    }
    behavior.links.clear();
    behavior.links.reserve(linkCount);

    // Read each link
    for (int i = 0; i < linkCount; ++i) {
        {
            std::ostringstream phase;
            phase << "behavior[" << context.layoutIndex << "].links[" << i << "].header";
            lastLoadPhase = phase.str();
            lastLoadDwordOffset = reader->GetCurrentPos();
        }
        Link link;

        if (context.useDevSectionedLayout) {
            const CKDWORD highlight = static_cast<CKDWORD>(reader->ReadInt());
            const CKDWORD type = static_cast<CKDWORD>(reader->ReadInt());
            link.serializedHighlight = highlight;
            link.serializedType = type;
            Format::DecodeDevSectionedLinkType(highlight, type, link.type, link.highlight);
            {
                std::ostringstream phase;
                phase << "behavior[" << context.layoutIndex << "].links[" << i
                      << "].header highlight=" << highlight << ",type=" << type;
                lastLoadPhase = phase.str();
            }
        } else {
            const CKDWORD rawType = reader->ReadDword();
            link.serializedType = rawType;
            Format::DecodeLegacyInlineLinkType(rawType, link.type, link.highlight);
            {
                std::ostringstream phase;
                phase << "behavior[" << context.layoutIndex << "].links[" << i
                      << "].header rawType=" << rawType;
                lastLoadPhase = phase.str();
            }
        }

        // Read link ID
        link.id = reader->ReadObjectID();

        // Read start endpoint
        link.start.id = reader->ReadObjectID();
        link.start.index = reader->ReadInt();
        link.start.type = static_cast<EndpointType>(reader->ReadDword());
        link.start.hasLegacyTypeCode = context.version < Format::DIRECT_ENDPOINT_TYPE_MIN_VERSION;
        {
            std::ostringstream phase;
            phase << "behavior[" << context.layoutIndex << "].links[" << i
                  << "].start linkId=" << link.id
                  << ",start=" << link.start.id << ":" << link.start.index
                  << ":" << static_cast<int>(link.start.type);
            lastLoadPhase = phase.str();
        }

        // Read control points
        const int pointCount = reader->ReadCount(2, 3);
        lastLoadDwordOffset = reader->GetCurrentPos() - 1;
        {
            std::ostringstream phase;
            phase << "behavior[" << context.layoutIndex << "].links[" << i
                  << "].points.count=" << pointCount
                  << ",linkCount=" << linkCount
                  << ",linkCountOffset=" << linkCountOffset
                  << ",devLayout=" << (context.useDevSectionedLayout ? 1 : 0)
                  << ",linkId=" << link.id
                  << ",start=" << link.start.id << ":" << link.start.index
                  << ":" << static_cast<int>(link.start.type)
                  << ",type=" << static_cast<int>(link.type)
                  << ",highlight=" << (link.highlight ? 1 : 0);
            lastLoadPhase = phase.str();
        }
        link.points.resize(pointCount);

        for (int j = 0; j < pointCount; ++j) {
            {
                std::ostringstream phase;
                phase << "behavior[" << context.layoutIndex << "].links[" << i << "].points[" << j << "]";
                lastLoadPhase = phase.str();
            }
            link.points[j].h = reader->ReadFloat();
            link.points[j].v = reader->ReadFloat();
        }

        // Read end endpoint
        {
            std::ostringstream phase;
            phase << "behavior[" << context.layoutIndex << "].links[" << i << "].end";
            lastLoadPhase = phase.str();
        }
        link.end.id = reader->ReadObjectID();
        link.end.index = reader->ReadInt();
        link.end.type = static_cast<EndpointType>(reader->ReadDword());
        link.end.hasLegacyTypeCode = link.start.hasLegacyTypeCode;

        // Add the link to the behavior
        behavior.links.push_back(link);
    }
}

void InterfaceData::LoadBehaviorOperations(SerializationContext &context, BehaviorData &behavior) {
    InterfaceChunkReader *reader = context.reader;

    // Read operation count
    const int opCount = reader->ReadCount(3);
    behavior.operations.clear();
    behavior.operations.reserve(opCount);

    // Read each operation
    for (int i = 0; i < opCount; ++i) {
        Operation op;

        // Read operation ID and position
        op.id = reader->ReadObjectID();
        op.hPos = reader->ReadFloat();
        op.vPos = reader->ReadFloat();

        // Add the operation to the behavior
        behavior.operations.push_back(op);
    }
}

void InterfaceData::LoadBehaviorComments(SerializationContext &context, BehaviorData &behavior) {
    InterfaceChunkReader *reader = context.reader;

    // Read comment count
    const int commentCount = reader->ReadCount(
        context.useDevSectionedLayout || CommentStyleIsStored(context.version) ? 6 : 5);
    behavior.comments.clear();
    behavior.comments.reserve(commentCount);

    // Read each comment
    for (int i = 0; i < commentCount; ++i) {
        Comment comment;

        // Dev stores wrapper position followed by expanded width/height.
        comment.hPos = reader->ReadFloat();
        comment.vPos = reader->ReadFloat();
        comment.width = reader->ReadFloat();
        comment.height = reader->ReadFloat();

        reader->ReadStringDwords(comment.text, comment.serializedTextDwords);

        // Older sectioned records have a trailing int, not inline styleFlags.
        if (context.useDevSectionedLayout) {
            comment.sectionedTrailingValue = reader->ReadInt();
        } else if (CommentStyleIsStored(context.version)) {
            comment.styleFlags = reader->ReadDword();
        }

        // Add the comment to the behavior
        behavior.comments.push_back(comment);
    }
}

namespace {
bool SeekBodySection(InterfaceChunkReader *reader, BehaviorData &behavior,
                     CKDWORD version, CKDWORD index, CKDWORD plainBase) {
    const CKDWORD id = Format::SectionedIdentifier(version, index, plainBase);
    if (!reader->SeekIdentifier(id)) return false;
    behavior.sectionedSections.push_back(id);
    return true;
}

void ReadParameterTable(InterfaceChunkReader *reader, CKDWORD version, bool hasStyles,
                        std::vector<Parameter> &parameters, int sourceWords = 0) {
    const int count = reader->ReadCount(2 + (hasStyles ? 1 : 0) + sourceWords);
    parameters.resize(count);
    for (auto &param : parameters) {
        param.hPos = reader->ReadInt();
        param.vPos = reader->ReadInt();
    }
    if (hasStyles) {
        for (auto &param : parameters) {
            param.serializedStyle = reader->ReadInt();
            param.hasSerializedStyle = true;
            param.style = static_cast<ParameterStyle>(Format::DecodeParameterStyle(version, param.serializedStyle));
        }
    }
}

int ParameterStyleForSave(CKDWORD version, const Parameter &param) {
    const int style = static_cast<int>(param.style);
    return param.hasSerializedStyle && Format::DecodeParameterStyle(version, param.serializedStyle) == style
        ? param.serializedStyle : Format::EncodeParameterStyle(version, style);
}

bool ParameterTableHasStyles(CKDWORD version) {
    return !Format::VersionUsesSections(version) || version >= Format::SECTIONED_PARAMETER_STYLE_MIN_VERSION;
}

bool BehaviorBodyIsStored(CKDWORD version, CKDWORD flags) {
    return Format::VersionUsesSections(version) || !(flags & Format::BEHAVIOR_FLAG_BODY_OMITTED);
}

bool ParameterTablesAreStored(CKDWORD version, bool isBuildingBlock) {
    return Format::VersionUsesSections(version) || !isBuildingBlock;
}

bool InputOutputParameterTablesAreStored(CKDWORD version, bool isNotScript, bool isBuildingBlock) {
    return isNotScript && (Format::VersionUsesSections(version) || !isBuildingBlock ||
                           version == Format::LEGACY_GRAPH_TABLE_VERSION);
}

bool HasInputOutputParameterPositions(const BehaviorData &behavior, bool folded) {
    return folded
        ? !behavior.outwardInputs.empty() || !behavior.foldedInputRows.empty() ||
          !behavior.outwardOutputs.empty() || !behavior.foldedOutputRows.empty()
        : !behavior.inwardInputs.empty() || !behavior.expandedInputRows.empty() ||
          !behavior.inwardOutputs.empty() || !behavior.expandedOutputRows.empty();
}

void WriteParameterTable(CKStateChunk *chunk, CKDWORD version, bool hasStyles,
                         const std::vector<Parameter> &parameters) {
    chunk->WriteInt(parameters.size());
    for (const auto &param : parameters) {
        chunk->WriteInt(param.hPos);
        chunk->WriteInt(param.vPos);
    }
    if (hasStyles) {
        for (const auto &param : parameters) {
            chunk->WriteInt(ParameterStyleForSave(version, param));
        }
    }
}

void ReadParameterPositions(InterfaceChunkReader *reader, std::vector<int> &columns, std::vector<int> &rows) {
    const int count = reader->ReadCount(2);
    columns.resize(count);
    rows.resize(count);
    for (int i = 0; i < count; ++i) {
        columns[i] = reader->ReadInt();
        rows[i] = reader->ReadInt();
    }
}

void WriteParameterPositions(CKStateChunk *chunk, const std::vector<int> &columns,
                      const std::vector<int> &rows) {
    chunk->WriteInt(columns.size());
    for (size_t i = 0; i < columns.size(); ++i) {
        chunk->WriteInt(columns[i]);
        chunk->WriteInt(i < rows.size() ? rows[i] : 0);
    }
}

CKERROR CheckParameterStylesForSave(CKDWORD version, const std::vector<Parameter> &parameters) {
    for (const auto &param : parameters) {
        if (!ParameterTableHasStyles(version)) {
            // NAME is the library's constructor default, not a stored or verified Dev default.
            if (param.style != PARAM_STYLE_NAME || param.hasSerializedStyle) return CKERR_NOTIMPLEMENTED;
        } else if (Format::DecodeParameterStyle(version, ParameterStyleForSave(version, param)) !=
                   static_cast<int>(param.style)) {
            return CKERR_NOTIMPLEMENTED;
        }
    }
    return CK_OK;
}

CKERROR CheckBehaviorBodyForSave(CKDWORD version, bool isNotScript, bool isBuildingBlock,
                               const BehaviorData &behavior) {
    const bool legacyEndpointCodes = version < Format::DIRECT_ENDPOINT_TYPE_MIN_VERSION;
    for (const auto &link : behavior.links) {
        // The old conversion table is not established. Neither a cast nor a
        // version edit can convert stored codes into direct endpoint types.
        if (link.start.hasLegacyTypeCode != legacyEndpointCodes ||
            link.end.hasLegacyTypeCode != legacyEndpointCodes)
            return CKERR_NOTIMPLEMENTED;
    }
    const bool sections = Format::VersionUsesSections(version);
    const bool hasFoldedPositions = HasInputOutputParameterPositions(behavior, true);
    const bool hasExpandedPositions = HasInputOutputParameterPositions(behavior, false);
    const bool hasParameters = !behavior.localParams.empty() || !behavior.sharedParams.empty();
    if (!BehaviorBodyIsStored(version, behavior.flags) &&
        (!behavior.links.empty() || !behavior.operations.empty() || !behavior.comments.empty() ||
         hasParameters || hasExpandedPositions || hasFoldedPositions))
        return CKERR_NOTIMPLEMENTED;
    if (!ParameterTablesAreStored(version, isBuildingBlock) && hasParameters)
        return CKERR_NOTIMPLEMENTED;
    if (!InputOutputParameterTablesAreStored(version, isNotScript, isBuildingBlock) &&
        (hasExpandedPositions || hasFoldedPositions))
        return CKERR_NOTIMPLEMENTED;
    if (sections) {
        if (version < Format::SECTIONED_SHARED_PARAMETERS_MIN_VERSION && !behavior.sharedParams.empty())
            return CKERR_NOTIMPLEMENTED;
        if (version < Format::SECTIONED_FOLDED_IO_MIN_VERSION && hasFoldedPositions)
            return CKERR_NOTIMPLEMENTED;
    }
    // Missing legacy vPos entries retain the zero-fill policy; excess entries have no wire slot.
    if (behavior.expandedInputRows.size() > behavior.inwardInputs.size() ||
        behavior.foldedInputRows.size() > behavior.outwardInputs.size() ||
        behavior.expandedOutputRows.size() > behavior.inwardOutputs.size() ||
        behavior.foldedOutputRows.size() > behavior.outwardOutputs.size())
        return CKERR_INVALIDPARAMETER;
    for (const auto &comment : behavior.comments) {
        if ((!CommentStyleIsStored(version) && comment.styleFlags != 0) ||
            (!sections && comment.sectionedTrailingValue != 0))
            return CKERR_NOTIMPLEMENTED;
    }
    CKERROR result = CheckParameterStylesForSave(version, behavior.localParams);
    if (result != CK_OK) return result;
    result = CheckParameterStylesForSave(version, behavior.sharedParams);
    if (result != CK_OK) return result;

    const bool tagged = sections && version >= Format::SECTIONED_TAGGED_SOURCE_MIN_VERSION;
    for (const auto &param : behavior.sharedParams) {
        // Untagged formats have implicit ObjectID storage. Only the library's implicit tag 1
        // can survive reloading; other tag values have no wire field in the selected format.
        if (!tagged && (param.sourceOwnerTag != 1 || param.sourceTag != 1)) return CKERR_NOTIMPLEMENTED;
        if (!sections && version >= Format::PARAMETER_SOURCE_ID_MIN_VERSION &&
            (param.sourceOwnerId != 0 || param.sourceMappingValue != 0)) return CKERR_NOTIMPLEMENTED;
    }
    return CK_OK;
}
} // namespace

void InterfaceData::LoadBehaviorParameters(SerializationContext &context, BehaviorData &behavior) {
    InterfaceChunkReader *reader = context.reader;
    const bool sections = context.useDevSectionedLayout;
    const bool hasStyles = ParameterTableHasStyles(context.version);
    if (!sections || SeekBodySection(reader, behavior, context.version, context.layoutIndex,
                                     Format::SECTION_LOCAL_PARAMETERS)) {
        ReadParameterTable(reader, context.version, hasStyles, behavior.localParams);
    }
    if (sections && context.version < Format::SECTIONED_SHARED_PARAMETERS_MIN_VERSION) return;
    if (!sections || SeekBodySection(reader, behavior, context.version, context.layoutIndex,
                                     Format::SECTION_SHARED_PARAMETERS)) {
        const int sourceWords = !sections && context.version >= Format::PARAMETER_SOURCE_ID_MIN_VERSION
            ? 1 : (sections && context.version >= Format::SECTIONED_TAGGED_SOURCE_MIN_VERSION ? 5 : 3);
        ReadParameterTable(reader, context.version, hasStyles, behavior.sharedParams, sourceWords);
        for (auto &param : behavior.sharedParams) {
            if (!sections && context.version >= Format::PARAMETER_SOURCE_ID_MIN_VERSION) {
                param.sourceId = reader->ReadObjectID();
            } else {
                const bool tagged = sections && context.version >= Format::SECTIONED_TAGGED_SOURCE_MIN_VERSION;
                if (tagged) param.sourceOwnerTag = reader->ReadInt();
                param.sourceOwnerId = param.sourceOwnerTag ? reader->ReadObjectID() : reader->ReadInt();
                if (tagged) param.sourceTag = reader->ReadInt();
                param.sourceId = param.sourceTag ? reader->ReadObjectID() : reader->ReadInt();
                param.sourceMappingValue = reader->ReadInt();
            }
        }
    }
}

void InterfaceData::LoadBehaviorGraph(SerializationContext &context, BehaviorData &behavior) {
    InterfaceChunkReader *reader = context.reader;
    const bool sections = context.useDevSectionedLayout;
    const bool hasFoldedPositions = !sections || context.version >= Format::SECTIONED_FOLDED_IO_MIN_VERSION;
    if (!sections || SeekBodySection(reader, behavior, context.version, context.layoutIndex,
                                     Format::SECTION_INPUT_POSITIONS)) {
        ReadParameterPositions(reader, behavior.inwardInputs, behavior.expandedInputRows);
        if (hasFoldedPositions)
            ReadParameterPositions(reader, behavior.outwardInputs, behavior.foldedInputRows);
        behavior.isBehaviorGraph = true;
    }
    if (!sections || SeekBodySection(reader, behavior, context.version, context.layoutIndex,
                                     Format::SECTION_OUTPUT_POSITIONS)) {
        ReadParameterPositions(reader, behavior.inwardOutputs, behavior.expandedOutputRows);
        if (hasFoldedPositions)
            ReadParameterPositions(reader, behavior.outwardOutputs, behavior.foldedOutputRows);
        behavior.isBehaviorGraph = true;
    }
}

void InterfaceData::LoadBehaviorBody(SerializationContext &context, BehaviorData &behavior) {
    if (context.useDevSectionedLayout) {
        LoadSectionedBehaviorBody(context, behavior);
    } else {
        LoadInlineBehaviorBody(context, behavior);
    }
}

void InterfaceData::LoadInlineBehaviorBody(
    SerializationContext &context,
    BehaviorData &behavior) {
    lastLoadPhase = "behavior[" + std::to_string(context.layoutIndex) + "].links";
    LoadBehaviorLinks(context, behavior);
    lastLoadPhase = "behavior[" + std::to_string(context.layoutIndex) + "].operations";
    LoadBehaviorOperations(context, behavior);
    lastLoadPhase = "behavior[" + std::to_string(context.layoutIndex) + "].comments";
    LoadBehaviorComments(context, behavior);

    if (ParameterTablesAreStored(context.version, context.isBuildingBlock)) {
        lastLoadPhase = "behavior[" + std::to_string(context.layoutIndex) + "].parameters";
        LoadBehaviorParameters(context, behavior);
    }

    if (InputOutputParameterTablesAreStored(context.version, context.isNotScript, context.isBuildingBlock)) {
        lastLoadPhase = "behavior[" + std::to_string(context.layoutIndex) + "].graph";
        LoadBehaviorGraph(context, behavior);
    }
}

void InterfaceData::LoadSectionedBehaviorBody(SerializationContext &context, BehaviorData &behavior) {
    InterfaceChunkReader *reader = context.reader;
    behavior.hasSectionedSections = true;
    if (SeekBodySection(reader, behavior, context.version, context.layoutIndex, 0x03000000))
        LoadBehaviorLinks(context, behavior);
    if (SeekBodySection(reader, behavior, context.version, context.layoutIndex, 0x02000000))
        LoadBehaviorOperations(context, behavior);
    if (SeekBodySection(reader, behavior, context.version, context.layoutIndex, 0x08000000))
        LoadBehaviorComments(context, behavior);
    LoadBehaviorParameters(context, behavior);
    if (InputOutputParameterTablesAreStored(context.version, context.isNotScript, context.isBuildingBlock))
        LoadBehaviorGraph(context, behavior);
    if (SeekBodySection(reader, behavior, context.version, context.layoutIndex, Format::SECTION_SCRIPT_FLAG)) {
        behavior.sectionedScriptFlag = reader->ReadInt();
    }
    behavior.flags = SectionedBehaviorFlags(context.version, context.isNotScript,
        behavior.folded, behavior.sectionedScriptFlag);
}

CKERROR InterfaceData::LoadExtraData(SerializationContext &context) {
    InterfaceChunkReader *reader = context.reader;

    // Check for extra data identifiers
    int extraVersion = 0;
    if (reader->SeekIdentifier(Format::EXTRA_DATA_V3_MARKER)) {
        extraVersion = Format::ExtraDataVersionFromMarker(Format::EXTRA_DATA_V3_MARKER);
    } else if (reader->SeekIdentifier(Format::EXTRA_DATA_V2_MARKER)) {
        extraVersion = Format::ExtraDataVersionFromMarker(Format::EXTRA_DATA_V2_MARKER);
    } else if (reader->SeekIdentifier(Format::EXTRA_DATA_V1_MARKER)) {
        extraVersion = Format::ExtraDataVersionFromMarker(Format::EXTRA_DATA_V1_MARKER);
    } else {
        extraData.clear();
        return CK_OK; // No extra data
    }

    // Store extra data version
    extraDataVersion = extraVersion;

    // Read extra data count
    const int count = reader->ReadCount(1);
    extraData.clear();
    extraData.reserve(count);

    // Read each extra data entry
    for (int i = 0; i < count; ++i) {
        ExtraData data;

        // Read type
        data.type = static_cast<ExtraDataType>(reader->ReadDword());

        // Read type-specific data
        switch (data.type) {
        case EXTRA_DATA_NONE:
            break;
        case EXTRA_DATA_BEHAVIOR:
        case EXTRA_DATA_PARAMETER:
            data.id1 = reader->ReadObjectID();
            break;
        case EXTRA_DATA_CONNECTION:
            data.id1 = reader->ReadObjectID();
            data.id2 = reader->ReadObjectID();
            break;
        case EXTRA_DATA_VALUE:
            // Dev's loader and saver disagree about type 4 sub-data in v2/v3.
            // Do not silently choose an unverified record boundary.
            if (extraVersion >= 2) return CKERR_NOTIMPLEMENTED;
            data.value = reader->ReadInt();
            break;
        default:
            return CKERR_NOTIMPLEMENTED;
        }

        // Read sub-data if this extra data version stores it.
        if (data.type != EXTRA_DATA_NONE && Format::ExtraDataVersionHasSubData(extraVersion)) {
            const int size = reader->ReadCount(4);
            data.subData.reserve(size);

            for (int j = 0; j < size; ++j) {
                ExtraSubData subData;

                // Breakpoint comparison type, logical operator, left CKParameter.
                subData.value1 = Format::DecodeExtraSubDataValue(extraVersion, reader->ReadInt());
                subData.value2 = reader->ReadInt();
                subData.id1 = reader->ReadObjectID();

                // Right operand: CKParameter reference or comparison value buffer.
                if (subData.UsesParameterReference()) {
                    subData.id2 = reader->ReadObjectID();
                } else {
                    reader->ReadBuffer(subData.buffer, subData.serializedBufferDwords);
                }

                data.subData.push_back(subData);
            }
        }

        extraData.push_back(data);
    }
    return CK_OK;
}

CKBOOL InterfaceData::SaveBehaviorHeader(SerializationContext &context) {
    CKBehavior *beh = context.behavior;
    if (!beh) return FALSE;
    context.isBuildingBlock = (beh->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    CKStateChunk *chunk = context.chunk;
    const BehaviorData &behavior = GetBehaviorForContext(context);
    context.flags = BehaviorFlagsForSave(behavior);

    if (context.useDevSectionedLayout) {
        chunk->WriteInt(behavior.sectionedElementId);
        chunk->WriteInt(behavior.sectionedParentElementId);
        chunk->WriteObject(beh);
        // Dev treats any nonzero state as folded. Keep its original encoding
        // unless folded was edited; a newly folded record uses canonical 1.
        chunk->WriteInt(behavior.folded ?
            (behavior.sectionedState != 0 ? behavior.sectionedState : 1) : 0);
    } else {
        chunk->WriteObject(beh);
        chunk->WriteDword(context.flags);
        chunk->WriteDword(context.isNotScript ? behavior.depth : header.scriptIndex);
    }
    chunk->WriteFloat(behavior.rect.hPos);
    chunk->WriteFloat(behavior.rect.vPos);
    if (!context.isNotScript) {
        if (ScriptStartPositionIsStored(context.version, context.isNotScript)) {
            chunk->WriteFloat(header.hStartPos);
            chunk->WriteFloat(header.vStartPos);
        }
        if (ScriptHeightIsStored(context.version, context.isNotScript))
            chunk->WriteFloat(header.vSize);
        InterfaceBitmap::Write(chunk, header.snapshot, header.snapshotStorage);
        if (ScriptColorIsStored(context.version, context.isNotScript))
            chunk->WriteDword(header.color);
    } else {
        chunk->WriteFloat(behavior.rect.hSize);
        chunk->WriteFloat(behavior.rect.vSize);
        chunk->WriteFloat(behavior.hExpandSize);
        chunk->WriteFloat(behavior.vExpandSize);
        if (BehaviorSnapshotIsStored(context.version, context.isNotScript))
            InterfaceBitmap::Write(chunk, behavior.sectionedSnapshot, behavior.sectionedSnapshotStorage);
    }
    return TRUE;
}

void InterfaceData::SaveBehaviorLinks(SerializationContext &context) {
    CKStateChunk *chunk = context.chunk;
    const BehaviorData &behavior = GetBehaviorForContext(context);

    // Write link count and data
    chunk->WriteInt(behavior.links.size());
    for (const auto &link : behavior.links) {
        if (context.useDevSectionedLayout) {
            const CKDWORD highlight = link.highlight ? (link.serializedHighlight ? link.serializedHighlight : 1) : 0;
            chunk->WriteDword(highlight);
            chunk->WriteDword((link.serializedType & ~LINK_TYPE_MASK) | (static_cast<CKDWORD>(link.type) & LINK_TYPE_MASK));
        } else {
            chunk->WriteDword((link.serializedType & ~(LINK_TYPE_MASK | LINK_FLAG_HIGHLIGHT)) |
                              Format::EncodeLegacyInlineLinkType(link.type, link.highlight));
        }
        chunk->WriteObjectID(link.id);
        chunk->WriteObjectID(link.start.id);
        chunk->WriteInt(link.start.index);
        chunk->WriteInt(link.start.type);
        chunk->WriteInt(link.points.size());
        for (const auto &point : link.points) {
            chunk->WriteFloat(point.h);
            chunk->WriteFloat(point.v);
        }
        chunk->WriteObjectID(link.end.id);
        chunk->WriteInt(link.end.index);
        chunk->WriteInt(link.end.type);
    }
}

void InterfaceData::SaveBehaviorOperations(SerializationContext &context) {
    CKStateChunk *chunk = context.chunk;
    const BehaviorData &behavior = GetBehaviorForContext(context);

    // Write operation count and data
    chunk->WriteInt(behavior.operations.size());
    for (const auto &op : behavior.operations) {
        chunk->WriteObjectID(op.id);
        chunk->WriteFloat(op.hPos);
        chunk->WriteFloat(op.vPos);
    }
}

void InterfaceData::SaveBehaviorComments(SerializationContext &context) {
    CKStateChunk *chunk = context.chunk;
    const BehaviorData &behavior = GetBehaviorForContext(context);

    // Write comment count and data
    chunk->WriteInt(behavior.comments.size());
    for (const auto &comment : behavior.comments) {
        // Size is independent of position, including for collapsed comments.
        chunk->WriteFloat(comment.hPos);
        chunk->WriteFloat(comment.vPos);
        chunk->WriteFloat(comment.width);
        chunk->WriteFloat(comment.height);

        Format::WriteStringDwords(chunk, comment.text, comment.serializedTextDwords);

        // The sectioned trailing value is separate from inline comment style bits.
        if (context.useDevSectionedLayout) {
            chunk->WriteInt(comment.sectionedTrailingValue);
        } else if (CommentStyleIsStored(context.version)) {
            chunk->WriteDword(comment.styleFlags);
        }
    }
}

void InterfaceData::SaveBehaviorParameters(SerializationContext &context) {
    CKStateChunk *chunk = context.chunk;
    const BehaviorData &behavior = GetBehaviorForContext(context);
    WriteParameterTable(chunk, context.version, true, behavior.localParams);
    WriteParameterTable(chunk, context.version, true, behavior.sharedParams);
    for (const auto &param : behavior.sharedParams) {
        if (context.version >= Format::PARAMETER_SOURCE_ID_MIN_VERSION) {
            chunk->WriteObjectID(param.sourceId);
        } else {
            chunk->WriteObjectID(param.sourceOwnerId);
            chunk->WriteObjectID(param.sourceId);
            chunk->WriteInt(param.sourceMappingValue);
        }
    }
}

void InterfaceData::SaveBehaviorGraph(SerializationContext &context) {
    CKStateChunk *chunk = context.chunk;
    const BehaviorData &behavior = GetBehaviorForContext(context);
    WriteParameterPositions(chunk, behavior.inwardInputs, behavior.expandedInputRows);
    WriteParameterPositions(chunk, behavior.outwardInputs, behavior.foldedInputRows);
    WriteParameterPositions(chunk, behavior.inwardOutputs, behavior.expandedOutputRows);
    WriteParameterPositions(chunk, behavior.outwardOutputs, behavior.foldedOutputRows);
}

void InterfaceData::SaveBehaviorBody(SerializationContext &context) {
    if (context.useDevSectionedLayout) {
        SaveSectionedBehaviorBody(context);
    } else {
        SaveInlineBehaviorBody(context);
    }
}

void InterfaceData::SaveInlineBehaviorBody(SerializationContext &context) {
    SaveBehaviorLinks(context);
    SaveBehaviorOperations(context);
    SaveBehaviorComments(context);

    if (ParameterTablesAreStored(context.version, context.isBuildingBlock))
        SaveBehaviorParameters(context);

    if (InputOutputParameterTablesAreStored(context.version, context.isNotScript, context.isBuildingBlock))
        SaveBehaviorGraph(context);
}

void InterfaceData::SaveSectionedBehaviorBody(SerializationContext &context) {
    CKStateChunk *chunk = context.chunk;
    const BehaviorData &behavior = GetBehaviorForContext(context);
    auto writeSection = [&](CKDWORD base, bool hasData) {
        const CKDWORD id = Format::SectionedIdentifier(context.version, context.layoutIndex, base);
        const bool wasPresent = std::find(behavior.sectionedSections.begin(),
            behavior.sectionedSections.end(), id) != behavior.sectionedSections.end();
        if (behavior.hasSectionedSections && !wasPresent && !hasData) return false;
        chunk->WriteIdentifier(id);
        return true;
    };

    if (writeSection(0x03000000, !behavior.links.empty())) SaveBehaviorLinks(context);
    if (writeSection(0x02000000, !behavior.operations.empty())) SaveBehaviorOperations(context);
    if (writeSection(0x08000000, !behavior.comments.empty())) SaveBehaviorComments(context);
    const bool hasStyles = ParameterTableHasStyles(context.version);
    if (writeSection(Format::SECTION_LOCAL_PARAMETERS, !behavior.localParams.empty()))
        WriteParameterTable(chunk, context.version, hasStyles, behavior.localParams);
    if (context.version >= Format::SECTIONED_SHARED_PARAMETERS_MIN_VERSION &&
        writeSection(Format::SECTION_SHARED_PARAMETERS, !behavior.sharedParams.empty())) {
        WriteParameterTable(chunk, context.version, hasStyles, behavior.sharedParams);
        const bool tagged = context.version >= Format::SECTIONED_TAGGED_SOURCE_MIN_VERSION;
        for (const auto &param : behavior.sharedParams) {
            if (tagged) chunk->WriteInt(param.sourceOwnerTag);
            if (!tagged || param.sourceOwnerTag) chunk->WriteObjectID(param.sourceOwnerId);
            else chunk->WriteInt(param.sourceOwnerId);
            if (tagged) chunk->WriteInt(param.sourceTag);
            if (!tagged || param.sourceTag) chunk->WriteObjectID(param.sourceId);
            else chunk->WriteInt(param.sourceId);
            chunk->WriteInt(param.sourceMappingValue);
        }
    }
    if (InputOutputParameterTablesAreStored(context.version, context.isNotScript, context.isBuildingBlock)) {
        if (writeSection(Format::SECTION_INPUT_POSITIONS,
                         !behavior.inwardInputs.empty() || !behavior.outwardInputs.empty())) {
            WriteParameterPositions(chunk, behavior.inwardInputs, behavior.expandedInputRows);
            if (context.version >= Format::SECTIONED_FOLDED_IO_MIN_VERSION)
                WriteParameterPositions(chunk, behavior.outwardInputs, behavior.foldedInputRows);
        }
        if (writeSection(Format::SECTION_OUTPUT_POSITIONS,
                         !behavior.inwardOutputs.empty() || !behavior.outwardOutputs.empty())) {
            WriteParameterPositions(chunk, behavior.inwardOutputs, behavior.expandedOutputRows);
            if (context.version >= Format::SECTIONED_FOLDED_IO_MIN_VERSION)
                WriteParameterPositions(chunk, behavior.outwardOutputs, behavior.foldedOutputRows);
        }
    }
    if (writeSection(Format::SECTION_SCRIPT_FLAG, behavior.sectionedScriptFlag != 0))
        chunk->WriteInt(behavior.sectionedScriptFlag);
}

void InterfaceData::SaveExtraData(SerializationContext &context) {
    CKStateChunk *chunk = context.chunk;

    if (extraData.empty() && extraDataVersion == 0)
        return;

    // Write extra data identifier based on version
    chunk->WriteIdentifier(Format::ExtraDataMarkerForVersion(extraDataVersion));

    // Write extra data count
    chunk->WriteInt(extraData.size());

    // Write each extra data entry
    for (const auto &data : extraData) {
        // Write type
        chunk->WriteInt(data.type);

        // Write type-specific data
        switch (data.type) {
        case EXTRA_DATA_BEHAVIOR:
        case EXTRA_DATA_PARAMETER:
            chunk->WriteObjectID(data.id1);
            break;
        case EXTRA_DATA_CONNECTION:
            chunk->WriteObjectID(data.id1);
            chunk->WriteObjectID(data.id2);
            break;
        case EXTRA_DATA_VALUE:
            chunk->WriteInt(data.value);
            break;
        default:
            break;
        }

        // Write sub-data if this extra data version stores it.
        if (data.type != EXTRA_DATA_NONE && Format::ExtraDataVersionHasSubData(extraDataVersion)) {
            chunk->WriteInt(data.subData.size());

            for (const auto &sub : data.subData) {
                const int value1 = Format::EncodeExtraSubDataValue(extraDataVersion, sub.value1);

                chunk->WriteInt(value1);
                chunk->WriteInt(sub.value2);
                chunk->WriteObjectID(sub.id1);

                if (sub.UsesParameterReference()) {
                    chunk->WriteObjectID(sub.id2);
                } else {
                    Format::WriteBufferDwords(chunk, sub.buffer, sub.serializedBufferDwords);
                }
            }
        }
    }
}

CKERROR InterfaceData::LoadFromChunk(CKBehavior *behavior, CKStateChunk *chunk) {
    return LoadFromChunk(behavior, chunk, false);
}

CKERROR InterfaceData::LoadFromChunk(CKBehavior *behavior, CKStateChunk *chunk, bool forceDevSectionedLayout) {
    lastLoadPhase = "entry";
    lastLoadDwordOffset = -1;
    if (!behavior || !chunk)
        return CKERR_INVALIDPARAMETER;

    // Initialize serialization context
    SerializationContext context;
    context.behavior = behavior;
    context.chunk = chunk;

    // Get behavior context
    CKContext *ckContext = behavior->GetCKContext();

    // Clear existing data
    Clear();
    std::shared_ptr<InterfaceChunkSections> serializedSections;
    const CKERROR sectionError = InterfaceChunkSections::Read(chunk, serializedSections);
    if (sectionError != CK_OK) return sectionError;
    lastLoadPhase = "start-read";

    // Determine if behavior is a script
    context.isNotScript = (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) == 0;

    // The scoped reader closes the SDK parser on success and every early exit.
    try {
        InterfaceChunkReader reader(chunk, *serializedSections, lastLoadDwordOffset);
        context.reader = &reader;

        // Read version
        lastLoadPhase = "version";
        context.version = 0;
        if (reader.SeekIdentifier(Format::LEGACY_TOP_LEVEL_MARKER))
            versionIdentifier = Format::LEGACY_TOP_LEVEL_MARKER;
        else if (reader.SeekIdentifier(Format::DEV_TOP_LEVEL_MARKER))
            versionIdentifier = Format::DEV_TOP_LEVEL_MARKER;
        else
            return CKERR_INVALIDPARAMETER;
        context.version = reader.ReadDword();

        // Store version for future reference
        version = context.version;
        context.useDevSectionedLayout = Format::VersionUsesSections(context.version);

        // Check version compatibility
        if (context.version < Format::MIN_SUPPORTED_VERSION) {
            ckContext->OutputToConsole((CKSTRING) "Deprecated version of interface chunk");
            return CKERR_OBSOLETEVIRTOOLS;
        }

        if (context.version > Format::MAX_SUPPORTED_VERSION) {
            ckContext->OutputToConsole((CKSTRING) "Unsupported version of interface chunk");
            return CKERR_NOTIMPLEMENTED;
        }
        if (context.version < Format::PLAIN_SECTION_MIN_VERSION &&
            versionIdentifier != Format::DEV_TOP_LEVEL_MARKER)
            return CKERR_INVALIDPARAMETER;

        // Layout comes from version, never from the top-level identifier.
        if (forceDevSectionedLayout && !context.useDevSectionedLayout)
            return CKERR_INVALIDPARAMETER;
        bool rootIsNotScript = context.isNotScript;
        if (context.useDevSectionedLayout) {
            if (reader.SeekIdentifier(Format::SectionedKindIdentifier(context.version, false)))
                rootIsNotScript = false;
            else if (reader.SeekIdentifier(Format::SectionedKindIdentifier(context.version, true)))
                rootIsNotScript = true;
            else
                return CKERR_INVALIDPARAMETER;
        }
        lastLoadPhase = "behavior-count";
        const int count = context.useDevSectionedLayout ? reader.ReadInt() : reader.ReadCount(9);
        // A sectioned count lives apart from its headers. Every supported header
        // has at least eight DWORDs; bound allocation by the entire data in that case.
        if (count < 1 || (context.useDevSectionedLayout &&
            count > chunk->GetDataSize() / static_cast<int>(8 * sizeof(CKDWORD))))
            throw InterfaceChunkReadError{CKERR_INVALIDPARAMETER,
                context.useDevSectionedLayout ? Format::SectionedKindIdentifier(context.version, rootIsNotScript)
                                              : versionIdentifier};
        useDevSectionedLayout = context.useDevSectionedLayout;
        behaviors.resize(count - 1);
        RefreshBehaviorCount();

        // Read each behavior
        for (int i = 0; i < count; ++i) {
            context.behaviorIndex = i == 0 ? 0 : static_cast<CKDWORD>(i - 1);
            context.layoutIndex = static_cast<CKDWORD>(i);
            context.isNotScript = i == 0
                ? rootIsNotScript : true;
            BehaviorData *targetBehavior = (i == 0) ? &rootBehavior : &behaviors[i - 1];

            {
                std::ostringstream phase;
                phase << "behavior[" << i << "].header";
                lastLoadPhase = phase.str();
            }
            const CKERROR headerError = LoadBehaviorHeader(context, *targetBehavior);
            if (headerError == CK_OK) {
                if (BehaviorBodyIsStored(context.version, context.flags)) {
                    std::ostringstream phase;
                    phase << "behavior[" << i << "].body";
                    lastLoadPhase = phase.str();
                    LoadBehaviorBody(context, *targetBehavior);
                }
                context.isNotScript = TRUE;
            } else {
                return headerError;
            }
        }

        // Load extra data
        lastLoadPhase = "extra-data";
        const CKERROR extraError = LoadExtraData(context);
        if (extraError != CK_OK) return extraError;
        reader.FinishSection();

        serializedSections->MarkKnown(versionIdentifier);
        if (context.useDevSectionedLayout) {
            serializedSections->MarkKnown(Format::SectionedKindIdentifier(version, rootIsNotScript));
            for (int i = 0; i < count; ++i) {
                const BehaviorData &record = i ? behaviors[i - 1] : rootBehavior;
                serializedSections->MarkKnown(Format::SectionedHeaderIdentifier(version, i, i ? true : rootIsNotScript));
                for (CKDWORD identifier : record.sectionedSections) serializedSections->MarkKnown(identifier);
            }
        }
        if (extraDataVersion) serializedSections->MarkKnown(Format::ExtraDataMarkerForVersion(extraDataVersion));
        serializedSections->FinishRead(version, rootIsNotScript);
        sectionStorage = serializedSections;

        lastLoadPhase = "complete";
        return CK_OK;
    } catch (const InterfaceChunkReadError &error) {
        std::ostringstream phase;
        phase << lastLoadPhase << ",section=0x" << std::hex << error.identifier;
        lastLoadPhase = phase.str();
        return error.code;
    }
}

// Utility functions for extracting and generating interface chunks
CKERROR ExtractInterfaceData(CKBehavior *behavior, InterfaceData &data) {
    if (!behavior)
        return CKERR_INVALIDPARAMETER;

    // Clear existing data
    data.Clear();

    // Get interface chunk
    CKStateChunk *chunk = behavior->GetInterfaceChunk();
    if (!chunk)
        return CKERR_NOTFOUND;

    // Load data from chunk
    return data.LoadFromChunk(behavior, chunk);
}

CKStateChunk *GenerateInterfaceChunk(InterfaceData &data, CKBehavior *behavior) {
    if (!behavior)
        return nullptr;

    // Create and start the chunk
    CKStateChunk *chunk = CreateCKStateChunk(-1);
    if (!chunk)
        return nullptr;

    // Generate the interface chunk
    if (data.SaveToChunk(behavior, chunk) != CK_OK) {
        DeleteCKStateChunk(chunk);
        return nullptr;
    }

    return chunk;
}

CKERROR InterfaceData::SaveToChunk(CKBehavior *behavior, CKStateChunk *chunk) {
    if (!behavior || !chunk)
        return CKERR_INVALIDPARAMETER;

    if (version < Format::MIN_SUPPORTED_VERSION) return CKERR_OBSOLETEVIRTOOLS;
    if (version > Format::MAX_SUPPORTED_VERSION) return CKERR_NOTIMPLEMENTED;
    if (useDevSectionedLayout && !Format::VersionUsesSections(version))
        return CKERR_INVALIDPARAMETER;
    if (versionIdentifier != Format::LEGACY_TOP_LEVEL_MARKER &&
        versionIdentifier != Format::DEV_TOP_LEVEL_MARKER)
        return CKERR_INVALIDPARAMETER;
    if (extraDataVersion < 0 || extraDataVersion > 3) return CKERR_INVALIDPARAMETER;
    for (const auto &entry : extraData) {
        if (entry.type < EXTRA_DATA_NONE || entry.type > EXTRA_DATA_VALUE ||
            (entry.type == EXTRA_DATA_VALUE && extraDataVersion >= 2))
            return CKERR_NOTIMPLEMENTED;
        if ((extraDataVersion < 2 || entry.type == EXTRA_DATA_NONE) && !entry.subData.empty())
            return CKERR_INVALIDPARAMETER;
        for (const auto &sub : entry.subData) {
            if (extraDataVersion == 2 && (sub.value1 == 4 || sub.value1 == 5))
                return CKERR_INVALIDPARAMETER;
            if (!sub.UsesParameterReference() &&
                sub.buffer.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
                return CKERR_INVALIDPARAMETER;
        }
    }

    // Check version/kind exclusions and parameter encodings before touching the output.
    // Do not inspect unparsed sections: their original bytes remain independently preserved.
    const bool rootIsNotScript = (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) == 0;
    CKERROR tableResult = CheckScriptHeaderForSave(version, rootIsNotScript, header);
    if (tableResult != CK_OK) return tableResult;
    if (rootIsNotScript && InterfaceBitmap::HasDataForSave(header.snapshot, header.snapshotStorage))
        return CKERR_NOTIMPLEMENTED;
    auto checkRecord = [&](CKBehavior *runtime, bool isNotScript, const BehaviorData &record) -> CKERROR {
        const CKERROR headerResult = CheckBehaviorHeaderForSave(version, isNotScript, record);
        if (headerResult != CK_OK) return headerResult;
        if (!BehaviorSnapshotIsStored(version, isNotScript) &&
            InterfaceBitmap::HasDataForSave(record.sectionedSnapshot, record.sectionedSnapshotStorage))
            return CKERR_NOTIMPLEMENTED;
        return CheckBehaviorBodyForSave(version, isNotScript,
            (runtime->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK) != 0, record);
    };
    tableResult = checkRecord(behavior, rootIsNotScript, rootBehavior);
    if (tableResult != CK_OK) return tableResult;
    std::vector<CKBehavior *> resolvedBehaviors;
    resolvedBehaviors.reserve(behaviors.size());
    for (const auto &record : behaviors) {
        CKBehavior *resolved = CKBehavior::Cast(behavior->GetCKContext()->GetObject(record.id));
        if (!resolved) return CKERR_NOTFOUND;
        tableResult = checkRecord(resolved, true, record);
        if (tableResult != CK_OK) return tableResult;
        resolvedBehaviors.push_back(resolved);
    }

    const auto validationIssues = ValidateInterfaceData(
        *this, behavior, InterfaceDataValidationStage::BeforeSaveToChunk);
    ReportInterfaceDataValidationIssues(behavior->GetCKContext(), validationIssues);
    // Reject invalid reconstruction before StartWrite. Historical chunks
    // retain their existing compatibility behavior and public editing APIs.
    if (generatedFromRuntimeGraph && !validationIssues.empty())
        return CKERR_INVALIDPARAMETER;

    // Initialize serialization context
    SerializationContext context;
    context.behavior = behavior;
    context.chunk = chunk;
    context.version = version;
    context.useDevSectionedLayout = Format::VersionUsesSections(version);
    context.isNotScript = (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) == 0;

    // Start writing to the chunk
    chunk->StartWrite();

    // Write header information
    chunk->WriteIdentifier(context.version < Format::PLAIN_SECTION_MIN_VERSION
        ? Format::DEV_TOP_LEVEL_MARKER : versionIdentifier);
    chunk->WriteDword(context.version);
    if (context.useDevSectionedLayout) {
        chunk->WriteIdentifier(Format::SectionedKindIdentifier(context.version, context.isNotScript));
    }
    const int serializedBehaviorCount = static_cast<int>(behaviors.size());
    const int count = serializedBehaviorCount + 1;
    chunk->WriteInt(count);

    // Process each behavior
    context.scriptIndex = header.scriptIndex;
    context.behaviorIndex = 0;

    // First, write the script root
    context.behavior = behavior;
    context.layoutIndex = 0;
    context.isNotScript = (context.behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) == 0;
    if (context.useDevSectionedLayout) {
        CKDWORD headerId = Format::SectionedHeaderIdentifier(context.version, context.layoutIndex, context.isNotScript);
        chunk->WriteIdentifier(headerId);
    }
    if (SaveBehaviorHeader(context) &&
        BehaviorBodyIsStored(context.version, context.flags)) {
        SaveBehaviorBody(context);
    }

    // Then, write each additional behavior
    for (int i = 0; i < serializedBehaviorCount; ++i) {
        context.behavior = resolvedBehaviors[i];

        context.behaviorIndex = i;
        context.layoutIndex = static_cast<CKDWORD>(i + 1);
        context.isNotScript = true; // Every record after the root has a non-script header.
        context.isBuildingBlock = (context.behavior->GetFlags() & CKBEHAVIOR_BUILDINGBLOCK) != 0;

        if (context.useDevSectionedLayout) {
            chunk->WriteIdentifier(Format::SectionedHeaderIdentifier(context.version, context.layoutIndex, true));
        }
        if (SaveBehaviorHeader(context) &&
            BehaviorBodyIsStored(context.version, context.flags)) {
            SaveBehaviorBody(context);
        }
    }

    // Write extra data if any
    SaveExtraData(context);

    // Finish writing
    chunk->CloseChunk();

    return sectionStorage ? sectionStorage->Merge(chunk, version,
        (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) == 0) : CK_OK;
}
