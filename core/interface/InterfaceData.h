#pragma once

#include "core/interface/InterfaceDataElements.h"

class InterfaceChunkSections;
class InterfaceChunkReader;

/**
 * In-memory representation of one InterfaceChunk.
 *
 * Public state mirrors stored Schematic View records or reconstruction-only
 * ownership fields. InterfaceChunk byte rules remain behind LoadFromChunk and
 * SaveToChunk.
 */
class InterfaceData {
public:
    CKDWORD version = InterfaceChunkFormat::CURRENT_VERSION;
    CKDWORD versionIdentifier = InterfaceChunkFormat::LEGACY_TOP_LEVEL_MARKER;
    bool useDevSectionedLayout = false; ///< Compatibility field; actual layout is selected by version
    bool generatedFromRuntimeGraph = false;
    std::string lastLoadPhase;
    int lastLoadDwordOffset = -1;

    Header header;
    BehaviorData rootBehavior;
    std::vector<BehaviorData> behaviors;
    int behaviorCount = 0;

    int extraDataVersion = 0;
    std::vector<ExtraData> extraData;
    std::vector<ElementObserver *> observers;

    InterfaceData();
    ~InterfaceData();

    BehaviorData &NewBehavior();
    void AddBehavior(BehaviorData &behavior);
    bool RemoveBehavior(CK_ID behaviorId);
    BehaviorData *FindBehavior(CK_ID id);
    Operation *FindOperation(CK_ID id);
    void AddExtraData(const ExtraData &data);
    void Clear();
    std::vector<CKDWORD> GetUnparsedSectionIdentifiers() const;
    /// Explicitly discard an unparsed section; parsed records are not removed.
    bool RemoveUnparsedSection(CKDWORD identifier);

    enum class ElementAction {
        Added,
        Removed,
        Modified
    };

    void AddObserver(ElementObserver *observer);
    void RemoveObserver(ElementObserver *observer);
    void NotifyObservers(InterfaceElement *element, ElementAction action) const;

    std::vector<InterfaceElement *> FindElementsById(CK_ID id);
    std::vector<InterfaceElement *> FindElementsAt(
        const Point &position,
        float tolerance = 5.0f);
    std::vector<Link *> FindLinksConnectedTo(CK_ID objectId);
    std::vector<BehaviorData *> GetSubTree(CK_ID rootId);
    BehaviorData *FindParentBehavior(CK_ID behaviorId);
    std::vector<BehaviorData *> FindChildBehaviors(CK_ID behaviorId);
    bool HasPath(CK_ID startId, CK_ID endId);
    std::vector<CK_ID> FindPath(CK_ID startId, CK_ID endId);

    /// Checked section reads; non-CK_OK may leave partial records, not valid save input.
    CKERROR LoadFromChunk(CKBehavior *behavior, CKStateChunk *chunk);
    CKERROR LoadFromChunk(
        CKBehavior *behavior,
        CKStateChunk *chunk,
        bool forceDevSectionedLayout);
    /// Excluded bodies/tables, unsupported parameter encodings, and absent Start/vSize/color/bitmap
    /// or Comment style/trailing fields with non-default values/data return CKERR_NOTIMPLEMENTED;
    /// excess IO vPos returns CKERR_INVALIDPARAMETER, unresolved/non-Behavior child IDs CKERR_NOTFOUND.
    /// These checks precede StartWrite; other failures do not guarantee an untouched output chunk.
    /// Unmapped old endpoint codes cannot cross the payload 0x10/0x11 encoding boundary.
    CKERROR SaveToChunk(CKBehavior *behavior, CKStateChunk *chunk);

    const char *GetLastLoadPhase() const { return lastLoadPhase.c_str(); }
    int GetLastLoadDwordOffset() const { return lastLoadDwordOffset; }

private:
    std::shared_ptr<InterfaceChunkSections> sectionStorage;
    void RefreshBehaviorCount();

    struct SerializationContext {
        CKBehavior *behavior = nullptr;
        CKStateChunk *chunk = nullptr;
        InterfaceChunkReader *reader = nullptr;
        bool isNotScript = false;
        bool isBuildingBlock = false;
        CKDWORD flags = 0;
        CKDWORD version = InterfaceChunkFormat::CURRENT_VERSION;
        CKDWORD scriptIndex = 0;
        CKDWORD behaviorIndex = 0;
        CKDWORD layoutIndex = 0;
        bool useDevSectionedLayout = false;
    };

    CKERROR LoadBehaviorHeader(SerializationContext &context, BehaviorData &behavior);
    void LoadBehaviorLinks(SerializationContext &context, BehaviorData &behavior);
    void LoadBehaviorOperations(SerializationContext &context, BehaviorData &behavior);
    void LoadBehaviorComments(SerializationContext &context, BehaviorData &behavior);
    void LoadBehaviorParameters(SerializationContext &context, BehaviorData &behavior);
    void LoadBehaviorGraph(SerializationContext &context, BehaviorData &behavior);
    void LoadBehaviorBody(SerializationContext &context, BehaviorData &behavior);
    void LoadInlineBehaviorBody(SerializationContext &context, BehaviorData &behavior);
    void LoadSectionedBehaviorBody(SerializationContext &context, BehaviorData &behavior);
    CKERROR LoadExtraData(SerializationContext &context);

    CKBOOL SaveBehaviorHeader(SerializationContext &context);
    void SaveBehaviorLinks(SerializationContext &context);
    void SaveBehaviorOperations(SerializationContext &context);
    void SaveBehaviorComments(SerializationContext &context);
    void SaveBehaviorParameters(SerializationContext &context);
    void SaveBehaviorGraph(SerializationContext &context);
    void SaveBehaviorBody(SerializationContext &context);
    void SaveInlineBehaviorBody(SerializationContext &context);
    void SaveSectionedBehaviorBody(SerializationContext &context);
    void SaveExtraData(SerializationContext &context);

    const BehaviorData &GetBehaviorForContext(const SerializationContext &context) const;
    BehaviorData &GetBehaviorForContext(SerializationContext &context);
};

CKERROR ExtractInterfaceData(CKBehavior *behavior, InterfaceData &data);
CKStateChunk *GenerateInterfaceChunk(InterfaceData &data, CKBehavior *behavior);
