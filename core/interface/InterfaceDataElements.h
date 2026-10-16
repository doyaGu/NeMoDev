#pragma once

#include <utility>
#include <vector>
#include <string>
#include <memory>

#include "CKDefines.h"
#include "CKBehavior.h"
#include "CKStateChunk.h"

#include "core/interface/InterfaceChunkFormat.h"
#include "core/schematic/Geometry.h"

// Forward declarations
class InterfaceData;
class InterfaceBitmap;

/**
 * @enum EndpointType
 * @brief Defines the types of endpoints for links
 */
enum EndpointType {
    ENDPOINT_POUT_SHORTCUT = 5,  ///< Parameter output link shortcut
    ENDPOINT_PIN           = 7,  ///< Parameter input
    ENDPOINT_POUT          = 8,  ///< Parameter output
    ENDPOINT_PLOCAL        = 9,  ///< Local parameter
    ENDPOINT_TARGET_PIN    = 10, ///< Target parameter input
    ENDPOINT_BIN           = 12, ///< Behavior input
    ENDPOINT_BOUT          = 13, ///< Behavior output
    ENDPOINT_START_BIN     = 26  ///< "Start" behavior input
};

/**
 * @enum ParameterStyle
 * @brief Defines the display style for parameters
 */
enum ParameterStyle : int {
    PARAM_STYLE_NAME      = 0x200,  ///< Display parameter name only
    PARAM_STYLE_COLLAPSED = 0x400,  ///< Parameter is closed/collapsed
    PARAM_STYLE_NAMEVALUE = 0x1000, ///< Display parameter name and value
    PARAM_STYLE_VALUE     = 0x2000  ///< Display parameter value only
};

/**
 * @enum CommentStyle
 * @brief Stored comment UI flags; only bit 0 is restored by Dev 2.5
 */
enum CommentStyle {
    COMMENT_STYLE_NORMAL      = 0x0, ///< Normal comment display
    COMMENT_STYLE_COLLAPSED   = 0x1, ///< Comment is collapsed to icon
    COMMENT_STYLE_LOCKED      = 0x2, ///< Historical API name; Dev semantics not verified
    COMMENT_STYLE_TRANSPARENT = 0x4  ///< Historical API name; Dev semantics not verified
};

/**
 * @enum ExtraDataType
 * @brief Types of extra data that can be stored in interface chunks
 */
enum ExtraDataType {
    EXTRA_DATA_NONE       = 0, ///< Null entry (no payload or sub-data count)
    EXTRA_DATA_PARAMETER  = 1, ///< Parameter breakpoint: CKParameter reference
    EXTRA_DATA_BEHAVIOR   = 2, ///< Behavior breakpoint: CKBehavior reference
    EXTRA_DATA_CONNECTION = 3, ///< Link breakpoint: CKBehaviorLink and owning CKBehavior
    EXTRA_DATA_VALUE      = 4  ///< Legacy scalar; v2/v3 record boundary is unsupported
};

/// Normalized breakpoint comparison types; v2 wire codes >= 4 are shifted by 2.
enum ExtraDataComparisonType : int {
    EXTRA_COMPARE_EQUAL_VALUE             = 0,
    EXTRA_COMPARE_NOT_EQUAL_VALUE         = 1,
    EXTRA_COMPARE_EQUAL_PARAMETER         = 2,
    EXTRA_COMPARE_NOT_EQUAL_PARAMETER     = 3,
    EXTRA_COMPARE_LESS_VALUE              = 4,
    EXTRA_COMPARE_LESS_EQUAL_VALUE        = 5,
    EXTRA_COMPARE_GREATER_VALUE           = 6,
    EXTRA_COMPARE_GREATER_EQUAL_VALUE     = 7,
    EXTRA_COMPARE_LESS_PARAMETER          = 8,
    EXTRA_COMPARE_LESS_EQUAL_PARAMETER    = 9,
    EXTRA_COMPARE_GREATER_PARAMETER       = 10,
    EXTRA_COMPARE_GREATER_EQUAL_PARAMETER = 11
};

/// Combines this breakpoint condition with the preceding result, left to right.
enum ExtraDataLogicalOperator : int {
    EXTRA_LOGICAL_AND = 0,
    EXTRA_LOGICAL_OR  = 1,
    EXTRA_LOGICAL_XOR = 2
};

// Forward declarations for core classes
struct Header;
struct LinkEndpoint;
struct Link;
struct Operation;
struct Comment;
struct Parameter;
struct ExtraSubData;
struct ExtraData;
struct BehaviorData;

/// Historical compatibility name: one CKBehavior input/output PARAMETER
/// display position, not a CKBehaviorIO position. hPos is a grid column;
/// Dev interprets the sign of vPos to select the top/bottom display edge.
/// Keep the full integer pair when loading/editing/saving.
struct BehaviorIOPosition {
    int hPos = 0;
    int vPos = 0;
    BehaviorIOPosition() = default;
    BehaviorIOPosition(int h, int v) : hPos(h), vPos(v) {}
};

/**
 * @struct InterfaceElement
 * @brief Base class for all interface elements with common functionality
 */
struct InterfaceElement {
    CK_ID id = 0; ///< ID of the element

    InterfaceElement() = default;

    explicit InterfaceElement(CK_ID elementId) : id(elementId) {}

    virtual ~InterfaceElement() = default;
};

/**
 * @struct Header
 * @brief Represents the header of a behavior script
 */
struct Header : InterfaceElement {
    CKDWORD scriptIndex = 0;        ///< Inline Script header index; absent for sectioned or non-Script roots
    float vStart = 0.0f;            ///< Reconstruction-only Start offset; no InterfaceChunk field
    float hStartPos = 140.0f;       ///< Script Start hPos; absent in sectioned 0x05/0x06
    float vStartPos = 0.0f;         ///< Start vPos relative to Script top; same version condition as hStartPos
    float hStartSize = 30.0f;       ///< Reconstruction-only Start width; no InterfaceChunk field
    float vStartSize = 20.0f;       ///< Reconstruction-only Start height; no InterfaceChunk field
    float vSize = 0.0f;             ///< Script vSize; absent in sectioned versions before 0x0F
    CKDWORD color = InterfaceChunkFormat::DEFAULT_HEADER_COLOR; ///< Script color, stored from inline 0x14; default is library-only
    void *snapshot = nullptr;       ///< Bitmap handle; loaded handles are owned, caller-assigned handles are borrowed

    Point GetPosition() const;
    void SetPosition(float h, float v);
    /// Borrow a caller-owned handle, or retain the same loaded handle. Discard
    /// the previous encoded record; nullptr also clears an undecoded image.
    void SetSnapshot(void *bitmap);

private:
    friend class InterfaceData;
    std::shared_ptr<InterfaceBitmap> snapshotStorage; ///< Decoded handle and original SDK encoding
};

/**
 * @struct LinkEndpoint
 * @brief Represents an endpoint of a link in the behavior tree
 */
struct LinkEndpoint : InterfaceElement {
    int index = 0;                                    ///< Index of the endpoint within its owner
    EndpointType type = static_cast<EndpointType>(0); ///< Type of the endpoint
    /// For payload 0x05..0x10, type holds an unmapped stored code, not EndpointType.
    /// Set explicitly when constructing old wire records; not a conversion switch.
    bool hasLegacyTypeCode = false;

    LinkEndpoint() = default;

    LinkEndpoint(CK_ID endpointId, int endpointIndex, EndpointType endpointType)
        : InterfaceElement(endpointId), index(endpointIndex), type(endpointType) {}

    /**
     * @brief Convenience constructor from raw type value
     */
    LinkEndpoint(CK_ID endpointId, int endpointIndex, int endpointType)
        : InterfaceElement(endpointId), index(endpointIndex), type(static_cast<EndpointType>(endpointType)) {}

    /**
     * @brief Checks if this endpoint is a parameter input
     */
    bool IsParameterInput() const;

    /**
     * @brief Checks if this endpoint is a parameter output
     */
    bool IsParameterOutput() const;

    /**
     * @brief Checks if this endpoint is a parameter output shortcut
     */
    bool IsParameterOutputShortcut() const;

    /** @deprecated Use IsParameterOutputShortcut. */
    bool IsParameterShortCut() const { return IsParameterOutputShortcut(); }

    /**
     * @brief Checks if this endpoint is a local parameter
     */
    bool IsParameterLocal() const;

    /**
     * @brief Checks if this endpoint is a behavior input
     */
    bool IsBehaviorInput() const;

    bool IsStartBehaviorInput() const;

    /**
     * @brief Checks if this endpoint is a behavior output
     */
    bool IsBehaviorOutput() const;

    bool IsParameterRelated() const;
    bool IsBehaviorRelated() const;
    bool IsCompatibleWith(const LinkEndpoint &other) const;

};

/**
 * @struct Link
 * @brief Represents a connection between two points in the behavior tree
 */
struct Link : InterfaceElement {
    LinkType type = static_cast<LinkType>(0); ///< Type of the link (low 16 bits only)
    bool highlight = false;                   ///< Whether the link is in highlight mode
    CKDWORD serializedType = 0;               ///< Original type bits, including unrecognized bits
    CKDWORD serializedHighlight = 0;          ///< Original sectioned highlight value
    LinkEndpoint start;                       ///< Starting endpoint
    std::vector<Point> points;                ///< Control points for link routing
    LinkEndpoint end;                         ///< Ending endpoint

    /**
     * @brief Default constructor
     */
    Link() = default;

    /**
     * @brief Constructor with all fields
     * @param linkId The unique ID of the link
     * @param linkType The type of the link (behavior or parameter)
     * @param startPoint The starting endpoint
     * @param endPoint The ending endpoint
     * @param highlightMode Whether the link is in highlight mode (default: false)
     */
    Link(CK_ID linkId, LinkType linkType, LinkEndpoint startPoint, LinkEndpoint endPoint, bool highlightMode = false)
        : InterfaceElement(linkId), type(linkType), highlight(highlightMode),
          start(std::move(startPoint)), end(std::move(endPoint)) {}

    void AddControlPoint(const Point &point);
    void InsertControlPoint(int index, const Point &point);
    void RemoveControlPoint(int index);
    void UpdateControlPoint(int index, const Point &point);

    /**
     * @brief Checks if this is a behavior link
     */
    bool IsBehaviorLink() const;

    bool IsParameterLink() const;
    bool IsHighlighted() const;
    void SetHighlighted(bool highlighted);
    bool IsParameterOpLink() const;
    void Offset(float h, float v);
    void Scale(float factor, const Point &center);
    float GetPathLength() const;
    Point GetPointAlong(float t) const;
    bool PassesNear(const Point &point, float maxDistance) const;

};

/**
 * @struct Operation
 * @brief Represents a parameter operation in the behavior tree
 */
struct Operation : InterfaceElement {
    float hPos = 0.0f; ///< Horizontal position
    float vPos = 0.0f; ///< Vertical position

    Operation() = default;

    explicit Operation(CK_ID opId) : InterfaceElement(opId) {}

    Operation(CK_ID opId, float h, float v) : InterfaceElement(opId), hPos(h), vPos(v) {}

    void SetPosition(float h, float v);
    Point GetPosition() const;
};

/**
 * @struct Comment
 * @brief Represents a comment in the behavior tree
 */
struct Comment : InterfaceElement {
    float hPos = 0.0f;      ///< Horizontal position
    float vPos = 0.0f;      ///< Vertical position
    float width = 100.0f;   ///< Stored expanded width, independent of hPos and collapse state
    float height = 50.0f;   ///< Stored expanded height, independent of vPos and collapse state
    std::string text;       ///< Comment text content
    std::vector<CKDWORD> serializedTextDwords; ///< Original CKStateChunk string dwords, including padding
    CKDWORD styleFlags = 0; ///< Unmasked comment UI flags in version 0x16; Dev restores only bit 0
    int sectionedTrailingValue = 0; ///< Old sectioned record's final int; not a style bit mask

    Comment() = default;

    Comment(CK_ID commentId, float h, float v, float w, float h2, const std::string &commentText)
        : InterfaceElement(commentId), hPos(h), vPos(v), width(w), height(h2), text(commentText) {}

    /**
     * @brief Gets the rectangle that encloses the comment
     */
    Rect GetRect() const;

    void SetRect(const Rect &rect);
    bool IsCollapsed() const;
    bool IsLocked() const;      ///< Compatibility query for bit 0x2; not a verified Dev option
    bool IsTransparent() const; ///< Compatibility query for bit 0x4; not a verified Dev option
    void SetCollapsed(bool collapsed);
    void SetLocked(bool locked); ///< Compatibility edit of bit 0x2; Dev does not restore it
    void SetTransparent(bool transparent); ///< Compatibility edit of bit 0x4; Dev does not restore it
    void ResizeToFitText(
        float charWidth,
        float lineHeight,
        float hPadding = 10.0f,
        float vPadding = 10.0f);

};

/**
 * @struct Parameter
 * @brief Represents a parameter in the behavior tree
 */
struct Parameter : InterfaceElement {
    int hPos = 0;                            ///< Horizontal grid index (col), not pixels
    int vPos = 0;                            ///< Vertical grid index (row), not pixels
    ParameterStyle style = PARAM_STYLE_NAME; ///< Display style; NAME is a library default when no style array exists
    CK_ID sourceId = 0;                      ///< Source parameter ID for shortcuts
    CK_ID sourceOwnerId = 0;                 ///< First reference in pre-0x15 source mapping
    int sourceOwnerTag = 1;                  ///< Sectioned 0x0E..0x11: nonzero means ObjectID, zero means int; otherwise implicit 1
    int sourceTag = 1;                       ///< Same tagged mapping; untagged formats have implicit ObjectID storage
    int sourceMappingValue = 0;              ///< Old source mapping's final int, preserved verbatim
    int serializedStyle = 0;                 ///< Original style before old-version conversion
    bool hasSerializedStyle = false;

    Parameter() = default;

    Parameter(CK_ID paramId, ParameterStyle paramStyle) : InterfaceElement(paramId), style(paramStyle) {}

    Parameter(CK_ID paramId, int h, int v, ParameterStyle paramStyle = PARAM_STYLE_NAME)
        : InterfaceElement(paramId), hPos(h), vPos(v), style(paramStyle) {}

    /**
     * @brief Checks if this parameter is a shortcut
     */
    bool IsShortcut() const;

    void SetPosition(int h, int v);
    Point GetPosition() const;
    bool HasStyleFlag(ParameterStyle flag) const;
    void SetStyleFlag(ParameterStyle flag, bool value);
    bool ShowsName() const;
    bool ShowsValue() const;
    bool IsClosed() const;
    void SetClosed(bool closed);

};

/**
 * @struct ExtraSubData
 * @brief One parameter comparison in a Schematic View breakpoint condition
 */
struct ExtraSubData : InterfaceElement {
    // Historical public field names remain source-compatible and retain unknown codes.
    int value1 = 0;             ///< Normalized ExtraDataComparisonType
    int value2 = 0;             ///< ExtraDataLogicalOperator; ignored for the first condition
    CK_ID id1 = 0;              ///< CKParameter being compared (left operand)
    CK_ID id2 = 0;              ///< Comparison CKParameter (right operand), for reference types
    std::vector<CKBYTE> buffer; ///< Comparison value bytes, for non-reference types

    ExtraSubData() = default;

    /**
     * @brief Constructor for ID-based sub-data
     */
    ExtraSubData(int val1, int val2, CK_ID objId1, CK_ID objId2 = 0)
        : value1(val1), value2(val2), id1(objId1), id2(objId2) {}

    /**
     * @brief Constructor for buffer-based sub-data
     */
    ExtraSubData(int val1, int val2, CK_ID objId1, const std::vector<CKBYTE> &data)
        : value1(val1), value2(val2), id1(objId1), buffer(data) {}

    ExtraDataComparisonType GetComparisonType() const;
    ExtraDataLogicalOperator GetLogicalOperator() const;
    bool UsesParameterReference() const;
    // Setters change only the code; callers supply the matching id2 or buffer.
    void SetComparisonType(ExtraDataComparisonType comparison);
    void SetLogicalOperator(ExtraDataLogicalOperator logicalOperator);

private:
    friend class InterfaceData;
    // Original length-prefixed DWORDs, including padding. Not comparison bytes.
    // Save compares the effective buffer before reusing this encoding.
    std::vector<CKDWORD> serializedBufferDwords;
};

/**
 * @struct ExtraData
 * @brief A persisted Schematic View breakpoint definition (legacy type 4 excepted)
 */
struct ExtraData : InterfaceElement {
    ExtraDataType type = EXTRA_DATA_VALUE; ///< Type of extra data
    CK_ID id1 = 0;                         ///< First object ID
    CK_ID id2 = 0;                         ///< Second object ID (for connections)
    int value = 0;                         ///< Value (for value type)
    std::vector<ExtraSubData> subData;     ///< Sub-data elements

    ExtraData() = default;

    /**
     * @brief Constructor for behavior reference extra data
     */
    explicit ExtraData(CK_ID behaviorId) : type(EXTRA_DATA_BEHAVIOR), id1(behaviorId) {}

    /**
     * @brief Constructor for parameter reference extra data
     */
    explicit ExtraData(CK_ID parameterId, ExtraDataType extraType = EXTRA_DATA_PARAMETER)
        : type(extraType), id1(parameterId) {}

    /**
     * @brief Constructor for connection extra data
     */
    ExtraData(CK_ID sourceId, CK_ID targetId)
        : type(EXTRA_DATA_CONNECTION), id1(sourceId), id2(targetId) {}

    /**
     * @brief Constructor for value extra data
     */
    explicit ExtraData(int val) : type(EXTRA_DATA_VALUE), value(val) {}

    void AddSubData(const ExtraSubData &data);
};

class ElementObserver {
public:
    virtual ~ElementObserver() = default;
    virtual void OnElementAdded(InterfaceElement *element) = 0;
    virtual void OnElementRemoved(InterfaceElement *element) = 0;
    virtual void OnElementModified(InterfaceElement *element) = 0;
};

/**
 * @struct BehaviorData
 * @brief Represents a behavior in the tree
 */
struct BehaviorData : InterfaceElement {
    bool folded = false;          ///< Schematic View folded state; authoritative when saving either layout
    CKDWORD flags = 0;            ///< Inline raw flags, or derived sectioned UI flags; not CKBehavior flags
    // Inline 0x100 stores the Building Block Version Test difference display
    // state; it is not selection or CK_BEHAVIORPROTOTYPE_OBSOLETE.
    bool isUsingTarget = false;   ///< Runtime CKBehavior::IsUsingTarget(); not serialized as a UI flag
    // Runtime snapshot, never InterfaceChunk fields. Counts of -1 mean no
    // snapshot; do not infer them from links or parameter position tables.
    CKDWORD runtimeBehaviorFlags = 0; ///< CKBehavior::GetFlags(), not the UI flags above
    int runtimeInputCount = -1;       ///< CKBehavior::GetInputCount()
    int runtimeOutputCount = -1;      ///< CKBehavior::GetOutputCount()
    bool runtimeHasTargetParameter = false; ///< GetTargetParameter()!=nullptr, independent of IsUsingTarget()
    bool isBehaviorGraph = false; ///< Whether this is a behavior graph
    CKDWORD depth = 0;            ///< Inline non-Script hierarchy depth; absent for sectioned or Script headers
    CK_ID parentId = 0;           ///< Runtime parent behavior id; not serialized
    int runtimeSiblingIndex = -1; ///< Runtime CKBehavior::GetSubBehavior(i) index; not serialized
    Rect rect;                    ///< Size and position of the behavior
    float hExpandSize = 0.0f;     ///< Expanded horizontal size
    float vExpandSize = 0.0f;     ///< Expanded vertical size
    int sectionedElementId = 0;   ///< Old sectioned UI element identifier, not a CK_ID
    int sectionedParentElementId = 0;
    int sectionedState = 0;       ///< Old folded state: nonzero means folded; keep raw value while unchanged
    void *sectionedSnapshot = nullptr; ///< Old non-Script bitmap; loaded handles are owned, caller-assigned handles are borrowed
    bool hasSectionedSections = false;
    std::vector<CKDWORD> sectionedSections; ///< Present known body identifiers; not an opaque section copy
    int sectionedScriptFlag = 0;  ///< Raw 0x0A value: 1 hides Script; in 0x11 also sets non-Script 0x8000

    // Links
    std::vector<Link> links; ///< Links within this behavior

    // Operations
    std::vector<Operation> operations; ///< Operations within this behavior

    // Comments
    std::vector<Comment> comments; ///< Comments within this behavior

    // Parameters
    std::vector<Parameter> localParams; ///< Local parameters
    std::vector<Parameter> sharedParams; ///< Shared parameters

    // Historical names retained for source compatibility. These four tables
    // place PIN/POUT displays, NOT CKBehaviorIO. hPos is a grid column, not a
    // port index; vPos selects a display edge by sign, not an absolute Y row.
    std::vector<int> inwardInputs;   ///< Expanded input parameter hPos
    std::vector<int> outwardInputs;  ///< Folded input parameter hPos
    std::vector<int> inwardOutputs;  ///< Expanded output parameter hPos
    std::vector<int> outwardOutputs; ///< Folded output parameter hPos
    std::vector<int> expandedInputRows;  ///< Expanded input parameter vPos
    std::vector<int> foldedInputRows;    ///< Folded input parameter vPos
    std::vector<int> expandedOutputRows; ///< Expanded output parameter vPos
    std::vector<int> foldedOutputRows;   ///< Folded output parameter vPos

    BehaviorData() = default;

    explicit BehaviorData(CK_ID behaviorId) : InterfaceElement(behaviorId) {}

    /// Refresh the runtime snapshot after changing CKBehavior ports/flags/Target.
    /// nullptr clears it. No pointer is retained and no saved UI data changes.
    void UpdateRuntimeState(CKBehavior *behavior);

    // Historical query/edit names for input/output PARAMETER display tables.
    // No CKBehaviorIO geometry is stored in these tables.
    std::vector<BehaviorIOPosition> GetBehaviorInputPositions(bool foldedState) const;
    std::vector<BehaviorIOPosition> GetBehaviorOutputPositions(bool foldedState) const;
    void SetBehaviorInputPositions(bool foldedState, const std::vector<BehaviorIOPosition> &positions);
    void SetBehaviorOutputPositions(bool foldedState, const std::vector<BehaviorIOPosition> &positions);
    /// Same ownership and replacement rules as Header::SetSnapshot.
    void SetSectionedSnapshot(void *bitmap);

    void AddLink(const Link &link);
    void AddOperation(const Operation &operation);
    void AddLocalParameter(const Parameter &parameter);
    void AddSharedParameter(const Parameter &parameter);
    void AddComment(const Comment &comment);

    Link *FindLink(CK_ID linkId);
    std::vector<Link *> FindLinksConnectedTo(CK_ID objectId);
    Operation *FindOperation(CK_ID operationId);
    Parameter *FindLocalParameter(CK_ID parameterId);
    Parameter *FindSharedParameter(CK_ID parameterId);
    Parameter *FindParameter(CK_ID parameterId);
    Comment *FindComment(CK_ID commentId);
    std::vector<InterfaceElement *> FindElementsAt(
        const Point &position,
        float tolerance = 5.0f);

    void Reset();
    std::vector<InterfaceElement *> GetAllElements();
    bool RemoveElement(InterfaceElement *element);

private:
    friend class InterfaceData;
    std::shared_ptr<InterfaceBitmap> sectionedSnapshotStorage;
};
