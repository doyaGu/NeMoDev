#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>

#include "core/interface/InterfaceData.h"

// Forward declarations
class CKContext;
class CKBehavior;
class CKParameterIn;
class CKParameterOut;
class CKParameterLocal;
class CKParameter;
class CKParameterOperation;
class CKBehaviorLink;

//==============================================================================
// GraphBuilder
//==============================================================================
/**
 * @class GraphBuilder
 * @brief Constructs graph representation of Virtools behavior trees.
 *
 * Responsibilities:
 * - Traverses behavior hierarchy using BFS
 * - Maps behaviors, operations, and parameters
 * - Establishes parameter links between elements
 * - Builds the InterfaceData structure for serialization
 *
 * Usage:
 * @code
 *   InterfaceData data;
 *   GraphBuilder builder(data, context);
 *   builder.BuildGraph(rootBehavior);
 * @endcode
 */
class GraphBuilder {
public:
    //--------------------------------------------------------------------------
    // Construction
    //--------------------------------------------------------------------------

    /**
     * @brief Constructs a GraphBuilder
     * @param targetData Reference to the interface data to populate
     * @param context Pointer to the CK context for object lookup
     */
    GraphBuilder(InterfaceData &targetData, CKContext *context,
                 bool preserveSavedDirectParameterLinks = false);

    //--------------------------------------------------------------------------
    // Public Interface
    //--------------------------------------------------------------------------

    /**
     * @brief Builds the graph representation of a behavior tree
     * @param rootBehavior The root behavior to process
     */
    void BuildGraph(CKBehavior *rootBehavior);

private:
    //--------------------------------------------------------------------------
    // Internal Types
    //--------------------------------------------------------------------------

    /**
     * @struct ParameterPosition
     * @brief Represents a parameter's position within the behavior hierarchy
     */
    struct ParameterPosition {
        CK_ID id = 0;         ///< Owner element ID (behavior or operation)
        int index = 0;        ///< Parameter index within owner
        CK_ID behaviorId = 0; ///< Parent behavior graph ID
    };

    struct BehaviorQueueEntry {
        CKBehavior *behavior = nullptr;
        int depth = 0;
        int runtimeSiblingIndex = -1;
    };

    /// Maps parameter IDs to their position chains through the hierarchy
    using ParameterChain = std::unordered_map<CK_ID, std::vector<ParameterPosition>>;

    //--------------------------------------------------------------------------
    // Member Variables
    //--------------------------------------------------------------------------

    InterfaceData &m_Data;  ///< Target interface data structure
    CKContext *m_Context;   ///< CK context for object lookup
    bool m_PreserveSavedDirectParameterLinks = false;

    /// Maps behavior ID -> index in behaviors array (-1 for root)
    std::unordered_map<CK_ID, int> m_BehaviorMap;

    /// Maps behavior ID -> depth in the runtime CKBehavior tree
    std::unordered_map<CK_ID, int> m_BehaviorDepth;

    /// Tracked input parameters (set for uniqueness, vector for ordering)
    std::unordered_set<CK_ID> m_InputParamSet;
    std::vector<CK_ID> m_InputParams;

    /// Tracked output parameters (set for uniqueness, vector for ordering)
    std::unordered_set<CK_ID> m_OutputParamSet;
    std::vector<CK_ID> m_OutputParams;

    /// Local parameters displayed through shortcut boxes when rebuilding links.
    std::unordered_set<CK_ID> m_ShortcutLocalParams;

    /// Maps parent behavior ID and source parameter ID to sharedParams index.
    std::unordered_map<CK_ID, std::unordered_map<CK_ID, int>> m_SharedParameterIndexes;

    //--------------------------------------------------------------------------
    // Initialization
    //--------------------------------------------------------------------------

    /** @brief Resets all internal state for a fresh build */
    void InitializeState();

    //--------------------------------------------------------------------------
    // Behavior Tree Traversal (BFS)
    //--------------------------------------------------------------------------

    /** @brief Processes the behavior tree using BFS traversal */
    void ProcessBehaviorTree(CKBehavior *rootBehavior);

    /** @brief Enqueues sub-behaviors for BFS processing */
    void EnqueueSubBehaviors(CKBehavior *behavior, int depth,
                             std::queue<BehaviorQueueEntry> &queue);

    //--------------------------------------------------------------------------
    // Element Lookup Helpers
    //--------------------------------------------------------------------------

    /** @brief Gets behavior data by ID (returns root if index is -1) */
    BehaviorData &GetBehaviorData(CK_ID id);

    //--------------------------------------------------------------------------
    // Behavior Setup
    //--------------------------------------------------------------------------

    /** @brief Initializes a behavior data structure from a CK behavior */
    void SetupBehavior(BehaviorData &behaviorData, CKBehavior *behavior, int depth, int runtimeSiblingIndex);

    /** @brief Processes all parameters in a behavior (inputs, outputs, target) */
    void ProcessParameters(CKBehavior *behavior);

    /** @brief Processes parameters of a parameter operation */
    void ProcessOperationParameters(CKParameterOperation *operation);

    //--------------------------------------------------------------------------
    // Parameter Tracking
    //--------------------------------------------------------------------------

    /** @brief Adds an input parameter to tracking (maintains uniqueness) */
    void AddInputParameter(CK_ID paramId);

    /** @brief Adds an output parameter to tracking (maintains uniqueness) */
    void AddOutputParameter(CK_ID paramId);

    //--------------------------------------------------------------------------
    // Behavior Graph Elements
    //--------------------------------------------------------------------------

    /** @brief Adds behavior links (control flow) to a behavior */
    void AddBehaviorLinks(BehaviorData &behaviorData, CKBehavior *behavior);

    /** @brief Creates a Link from a CKBehaviorLink */
    Link CreateBehaviorLink(CKBehaviorLink *behaviorLink);

    /** @brief Adds parameter operations to a behavior */
    void AddOperations(BehaviorData &behaviorData, CKBehavior *behavior);

    /** @brief Adds local parameters to a behavior */
    void AddLocalParameters(BehaviorData &behaviorData, CKBehavior *behavior);

    //--------------------------------------------------------------------------
    // Parameter Position Resolution
    //--------------------------------------------------------------------------

    /**
     * @brief Gets position info for an input parameter
     * @param inputParam Input parameter
     * @param[out] owner Parent behavior graph
     * @return Position within the hierarchy
     */
    ParameterPosition GetInputParameterPosition(CKParameterIn *inputParam, CKBehavior **owner);

    /**
     * @brief Gets position info for an output parameter
     * @param outputParam Output parameter
     * @param[out] ownerBehavior Parent behavior graph
     * @return Position within the hierarchy
     */
    ParameterPosition GetOutputParameterPosition(CKParameterOut *outputParam, CKBehavior **ownerBehavior);

    /** @brief Gets position info for a local parameter */
    ParameterPosition GetLocalParameterPosition(CKParameterLocal *localParam);

    /** @brief Creates an input-parameter endpoint from a recorded position */
    static LinkEndpoint MakeInputParameterEndpoint(const ParameterPosition &position);

    /** @brief Creates an output-parameter endpoint from a recorded position */
    static LinkEndpoint MakeOutputParameterEndpoint(const ParameterPosition &position);

    /** @brief Creates a shortcut parameter position */
    ParameterPosition CreateShortcutParameterPosition(
        CK_ID behaviorId, CK_ID sourceId, ParameterStyle style);

    /** @brief Finds local variables that need shortcut boxes */
    void CollectShortcutLocalParameters();

    /** @brief Checks whether a local parameter should use a shortcut box */
    bool ShouldUseShortcutForLocal(CK_ID localParamId) const;

    /** @brief Mirrors Dev UI validation for direct parameter source links */
    bool ShouldCreateDirectParameterLink(CKParameterIn *inputParam, CKParameter *sourceParam) const;

    //--------------------------------------------------------------------------
    // Parameter Link Configuration
    //--------------------------------------------------------------------------

    /**
     * @brief Main entry point for parameter link configuration
     *
     * Establishes connections between parameters across different levels
     * of the behavior hierarchy by:
     * 1. Building chains of input parameters upwards
     * 2. Building chains of output parameters upwards
     * 3. Connecting these chains to form complete links
     */
    void ConfigureParameterLinks();

    //--------------------------------------------------------------------------
    // Parameter Chain Building
    //--------------------------------------------------------------------------

    /** @brief Builds chains for all tracked input parameters */
    void BuildInputParameterChains(ParameterChain &inputChain);

    /** @brief Builds a chain for a single input parameter */
    void BuildInputParameterChain(CKParameterIn *inputParam, ParameterChain &inputChain);

    /** @brief Builds chains for all tracked output parameters */
    void BuildOutputParameterChains(ParameterChain &outputChain);

    /** @brief Builds a chain for a single output parameter */
    void BuildOutputParameterChain(CKParameterOut *outputParam, ParameterChain &outputChain);

    //--------------------------------------------------------------------------
    // Parameter Chain Connection
    //--------------------------------------------------------------------------

    /** @brief Connects input chains to output chains to form links */
    void ConnectParameterChains(const ParameterChain &inputChain, const ParameterChain &outputChain);

    /** @brief Connects output-parameter destinations in a graph that displays both ends */
    void ConnectOutputDestinations(const ParameterChain &outputChain);

    /** @brief Connects an input parameter to its direct output source */
    void ConnectToDirectSource(CKParameterIn *inputParam, CKParameter *sourceParam,
                               const std::vector<ParameterPosition> &inputPositions,
                               const ParameterPosition &position,
                               const ParameterChain &outputChain);

    /** @brief Connects an input parameter to a shared input source */
    void ConnectToSharedSource(CKParameterIn *inputParam, CKParameterIn *sharedInput,
                               const std::vector<ParameterPosition> &inputPositions,
                               const ParameterChain &inputChain);

    /** @brief Attempts to connect parameters within the same behavior graph */
    bool ConnectParametersInSameBehavior(
        const std::vector<ParameterPosition> &inputPositions,
        const std::vector<ParameterPosition> &sourcePositions,
        CKParameter *sourceParam);

    /** @brief Finds the deepest input/source position pair with the same behaviorId */
    bool FindDeepestMatchingPositions(
        const std::vector<ParameterPosition> &inputPositions,
        const std::vector<ParameterPosition> &sourcePositions,
        const ParameterPosition *&inputPosition,
        const ParameterPosition *&sourcePosition) const;

    /** @brief Finds the deepest target/shared-source position pair */
    bool FindDeepestSharedInputPositions(
        const std::vector<ParameterPosition> &inputPositions,
        const std::vector<ParameterPosition> &sharedPositions,
        const ParameterPosition *&inputPosition,
        const ParameterPosition *&sharedPosition) const;

    /** @brief Creates a shortcut parameter for cross-behavior connections */
    void CreateParameterShortcut(const ParameterPosition &position, CKParameter *sourceParam);
};
