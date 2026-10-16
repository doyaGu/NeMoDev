#pragma once

#include <vector>
#include <unordered_set>
#include <cmath>
#include <algorithm>

#include "core/interface/InterfaceData.h"
#include "core/schematic/SchematicGrid.h"
#include "core/schematic/ParameterDisplayGeometry.h"

// Forward declarations
class CKContext;
class CKBehavior;

//==============================================================================
// LayoutCalculator
//==============================================================================
/**
 * @class LayoutCalculator
 * @brief Calculates visual layout of Virtools behavior trees.
 *
 * Responsibilities:
 * - Calculates behavior sizes and positions
 * - Positions operations and parameters
 * - Leaves Link::points handling to ApplyLinkRoutingPolicy
 *
 * Coordinate System (Binary-Verified):
 * - All public API and geometry calculations use PIXEL coordinates
 * - Parameter positions are serialized as integer grid indices (col/row) in 20px grid
 * - Operation positions are stored as float pixels, snapped to 20.0 grid
 *
 * Binary-verified snap formula (from CUIKBehaviorWindow::SnapToGrid):
 *   adjusted = rel - grid*0.5
 *   cell = (int)(adjusted / grid) + (adjusted > 0 ? 1 : 0)
 *   snapped = cell * grid + base
 *
 * Usage:
 * @code
 *   LayoutCalculator calculator(data, context);
 *   calculator.CalculateRuntimeGraphLayout(script);
 * @endcode
 */
class LayoutCalculator {
public:
    //--------------------------------------------------------------------------
    // Construction
    //--------------------------------------------------------------------------

    /**
     * @brief Constructs a LayoutCalculator
     * @param targetData Reference to the interface data to populate
     * @param context Pointer to the CK context for object lookup
     */
    LayoutCalculator(InterfaceData &targetData, CKContext *context);

    //--------------------------------------------------------------------------
    // Public Interface
    //--------------------------------------------------------------------------

    /**
     * @brief Calculates runtime graph layout without synthetic link routing.
     */
    void CalculateRuntimeGraphLayout(CKBehavior *script);

    /**
     * @brief Snaps a value to a grid (binary-verified formula from sub_4D8400)
     * @param value Value to snap
     * @param gridSize Grid cell size
     * @param base Base offset
     * @return Snapped value
     */
    static float SnapToGrid(float value, float gridSize, float base = 0.0f) {
        return SchematicGrid::SnapToGrid(value, gridSize, base);
    }

    /**
     * @brief Fallback grid position for a local parameter with no saved UI position
     *
     * LayoutLocalParameters starts fallback local params at one 20px grid
     * cell from the behavior origin, then appends each subsequent param one row
     * lower. Script UI local params are clamped later to at least grid column 9.
     */
    static Point FallbackLocalParameterGridPosition(int fallbackIndex) {
        return Point(1.0f, static_cast<float>(fallbackIndex + 1));
    }

    /**
     * @brief Script UI minimum local-parameter X grid.
     *
     * CUIKBehaviorWindow::LayoutLocalParameters clamps local parameter nodes in
     * a script UI parent to at least 2 * 20 + 140 pixels from the origin.
     */
    static int ScriptLocalParameterMinimumGridColumn() {
        return ParameterDisplayGeometry::SCRIPT_MINIMUM_COLUMN;
    }

    /**
     * @brief Top-level script reconstruction cursor offset in pixels.
     *
     * Verified in sub_538F00: cursor = script position + (3 * 20 + 280, 2 * 20).
     */
    static Point TopLevelBehaviorCursorOffset() {
        return Point(340.0f, 40.0f);
    }

    /**
     * @brief Child behavior graph reconstruction cursor offset in pixels.
     *
     * Verified in CUIKSchematicView::ReconstructBehaviorLinks: child cursor =
     * behavior position + (2 * 20 + flt_60D388, 2 * 20), where flt_60D388 is
     * 140.0f in Dev 2.5.
     */
    static Point ChildBehaviorCursorOffset() {
        return Point(180.0f, 40.0f);
    }

    /**
     * @brief Recursive child-graph horizontal cursor advance in pixels.
     *
     * Verified in CUIKSchematicView::ReconstructSaveLinks before recursing
     * into a child graph: recursiveCursor.x += layoutColumnCount * 20 + 3 * 20.
     * Sibling behavior placement restores the original X cursor after each
     * child and only accumulates Y.
     */
    static float BehaviorCursorHorizontalAdvance(int layoutColumnCount) {
        return static_cast<float>(layoutColumnCount) * 20.0f + 60.0f;
    }

    /**
     * @brief Minimum vertical advance after a reconstructed behavior graph.
     *
     * Verified in CUIKSchematicView::ReconstructSaveLinks: the recursive return
     * height is clamped against outputRowIndex * 20 + 2 * 20.
     */
    static float BehaviorCursorMinimumVerticalAdvance(int outputRowIndex) {
        return static_cast<float>(outputRowIndex) * 20.0f + 40.0f;
    }

    /**
     * @brief Layout column count from GetLayoutColumnCount.
     */
    static int LayoutColumnCount(float contentWidth) {
        return SchematicGrid::LayoutColumnCount(contentWidth);
    }

    /**
     * @brief Output row index from GetOutputRowIndex.
     */
    static int OutputRowIndex(float outputRowY) {
        if (outputRowY <= 0.0f) return 0;
        return static_cast<int>(outputRowY / VERTICAL_SPACING);
    }

    /**
     * @brief Default runtime-created behavior content width in 20px columns.
     *
     * CUIKSchematicView_CreateBehaviorWindowForBehavior(createMode=0) calls
     * sub_4FA350 with V*g_fIOYOffset to initialize the type-22 behavior
     * window to bounds (0, 0, 140, 100). CUIKBehaviorWindow::GetContentWidth
     * later reads the type-3 content element width, which InitializeBehaviorBox
     * derives as 140 - H = 120 px, i.e. 6 columns.
     */
    static int DefaultBehaviorContentColumns() {
        return 6;
    }

    /**
     * @brief Empirical runtime-reconstruction behavior title width estimate in 20px columns.
     *
     * The editor uses the created title UI element's bounds and then rounds the
     * resulting pixel width up in GetLayoutColumnCount. Until the exact font
     * metric path is fully mirrored, use the same round-up direction with a
     * rough 0.3 grid-column estimate per title character.
     */
    static int BehaviorNameColumnEstimate(size_t nameLength) {
        if (nameLength == 0) return 0;
        return std::max(1, static_cast<int>(std::ceil(static_cast<float>(nameLength) * 0.3f)));
    }

    /**
     * @brief Default behavior input-parameter display position.
     *
     * LayoutBehaviorElements starts behavior input parameter boxes at x=0,
     * y=-g_fVerticalSpacing relative to the behavior window and advances x by
     * g_fHorizontalSpacing per parameter when no saved UI slot exists.
     */
    static Point BehaviorInputParameterPosition(const Rect &behaviorRect, int inputIndex) {
        return Point(behaviorRect.hPos + static_cast<float>(std::max(0, inputIndex)) * HORIZONTAL_SPACING,
                     behaviorRect.vPos - VERTICAL_SPACING);
    }

    /**
     * @brief Default behavior output-parameter display position.
     *
     * Output parameter boxes are placed on the behavior content bottom edge.
     * When a target parameter is visible, the editor starts output parameters one grid
     * cell to the right so the target slot occupies the first column.
     */
    static Point BehaviorOutputParameterPosition(const Rect &behaviorRect,
                                                   int outputIndex,
                                                   bool isUsingTarget) {
        const int column = std::max(0, outputIndex) + (isUsingTarget ? 1 : 0);
        return Point(behaviorRect.hPos + static_cast<float>(column) * HORIZONTAL_SPACING,
                     behaviorRect.vPos + behaviorRect.vSize);
    }

    /**
     * @brief Runtime-rebuild parameter-operation box position.
     *
     * CUIKSchematicView_CollectRuntimeGraphLinkRecords places operation UI boxes
     * at the parent UI element position plus rebuildBaseX/rebuildBaseY, then
     * advances Y by 4 * g_fVerticalSpacing per operation. Script UI elements
     * (CUIK type 23) add an extra 140 px to X.
     */
    static Point ParameterOperationPosition(const Rect &parentRect,
                                               int operationIndex,
                                               bool parentIsScriptUi) {
        const float scriptOffset = parentIsScriptUi ? 140.0f : 0.0f;
        return Point(parentRect.hPos + HORIZONTAL_SPACING * 3.0f + scriptOffset,
                     parentRect.vPos + VERTICAL_SPACING * 2.0f +
                         static_cast<float>(std::max(0, operationIndex)) * VERTICAL_SPACING * 4.0f);
    }

    /**
     * @brief Runtime behavior cursor clamp from sub_4CF9E0's common branch.
     *
     * The editor converts the desired cursor to 20px grid cells, clamps it against the
     * behavior window layout column count and output row index, then writes the
     * clamped pixel position.
     */
    static Point ClampBehaviorCursorPosition(const Point &desiredCursor,
                                                int layoutColumnCount,
                                                int outputRowIndex) {
        const int desiredCol = desiredCursor.h <= 0.0f ? 0 : static_cast<int>(desiredCursor.h / HORIZONTAL_SPACING);
        const int desiredRow = OutputRowIndex(desiredCursor.v);
        const int actualCol = std::max(layoutColumnCount, desiredCol);
        const int actualRow = std::max(outputRowIndex, desiredRow);
        return Point(static_cast<float>(actualCol) * HORIZONTAL_SPACING,
                     static_cast<float>(actualRow) * VERTICAL_SPACING);
    }

    /**
     * @brief Sibling cursor advance after a reconstructed child graph.
     *
     * ReconstructSaveLinks restores the current graph's X cursor after each
     * child reconstruction and accumulates only the returned vertical span.
     */
    static Point AdvanceBehaviorCursor(const Point &cursor,
                                          int layoutColumnCount,
                                          int outputRowIndex,
                                          float childRecursiveHeight) {
        (void)layoutColumnCount;
        const float nextH = cursor.h;
        const float nextV = cursor.v + std::max(childRecursiveHeight,
                                                BehaviorCursorMinimumVerticalAdvance(outputRowIndex));
        return Point(nextH, nextV);
    }

    /**
     * @brief Place a behavior using the cursor clamp without changing its size.
     */
    static void PlaceBehaviorAtCursor(BehaviorData &behavior,
                                         const Point &desiredCursor,
                                         float contentWidth,
                                         float outputRowY) {
        const Point position = ClampBehaviorCursorPosition(
            desiredCursor,
            LayoutColumnCount(contentWidth),
            OutputRowIndex(outputRowY));
        behavior.rect.hPos = position.h;
        behavior.rect.vPos = position.v;
    }

    /**
     * @brief Place a behavior and advance the sibling reconstruction cursor.
     */
    static void PlaceBehaviorAndAdvanceCursor(BehaviorData &behavior,
                                                 Point &cursor,
                                                 float contentWidth,
                                                 float outputRowY,
                                                 float childRecursiveHeight) {
        PlaceBehaviorAtCursor(behavior, cursor, contentWidth, outputRowY);
        cursor = AdvanceBehaviorCursor(cursor,
                                          LayoutColumnCount(contentWidth),
                                          OutputRowIndex(outputRowY),
                                          childRecursiveHeight);
    }

    /**
     * @brief Cursor used when recursing into an expanded child behavior graph.
     */
    static Point ChildGraphCursor(const BehaviorData &behavior) {
        const Point offset = ChildBehaviorCursorOffset();
        return Point(behavior.rect.hPos + offset.h, behavior.rect.vPos + offset.v);
    }

    /**
     * @brief Reconstruction size of one non-root behavior from its runtime
     * IO/parameter counts and name (library estimate, not recovered Dev
     * sizing). Also used for Building Blocks inserted by editors.
     */
    static void CalculateBehaviorSize(BehaviorData &behaviorData, CKBehavior *behavior);

private:
    //==========================================================================
    // Layout Constants (Binary-Verified)
    //==========================================================================
    // All values in PIXELS unless otherwise noted.
    //==========================================================================

    /// @name Grid Constants
    /// @{
    static constexpr float HORIZONTAL_SPACING = SchematicGrid::SPACING; ///< g_fHorizontalSpacing @ 0x642B0C
    static constexpr float VERTICAL_SPACING = SchematicGrid::SPACING;   ///< g_fVerticalSpacing @ 0x642B10
    /// @}

    /// @name Expansion Constants
    /// @{
    static constexpr float EXPANSION_PADDING = 4.0f;
    static constexpr float BEHAVIOR_EXPANSION_FACTOR = 10.0f;
    /// @}

    //==========================================================================
    // Member Variables
    //==========================================================================

    InterfaceData &m_Data;   ///< Target interface data structure
    CKContext *m_Context;    ///< CK context for object lookup

    //==========================================================================
    // Layout Process Coordination
    //==========================================================================

    /** @brief Calculates behavior window sizes without placing behavior nodes */
    void CalculateBehaviorSizes(const std::vector<CK_ID> &behaviorIds);


    /** @brief Finalizes script header from runtime-reconstructed bounds */
    void FinalizeRuntimeGraphScriptLayout();

    //==========================================================================
    // Element Lookup
    //==========================================================================

    /** @brief Gets behavior data by ID (returns nullptr if not found) */
    BehaviorData *GetBehaviorData(CK_ID id) const;

    /** @brief Gets operation by ID (returns nullptr if not found) */
    Operation *GetOperation(CK_ID id) const;

    /** @brief Checks if an ID represents a parameter operation */
    bool IsOperation(CK_ID id) const;

    /** @brief Gets all behavior IDs in consistent order (root first) */
    std::vector<CK_ID> GetBehaviorIds() const;

    //==========================================================================
    // Size and Position Calculation
    //==========================================================================

    /** @brief Places direct children with the runtime reconstruction cursor model */
    float PlaceRuntimeGraphChildren(BehaviorData &parent, Point cursor);

    /** @brief Places behavior-link reachable children inside a single behavior graph */
    float PlaceRuntimeGraphFlowChildren(BehaviorData &parent,
                                   CK_ID currentBehaviorId,
                                   Point cursor,
                                   std::unordered_set<CK_ID> &visited,
                                   bool includeFallbackChildren);

    /** @brief Places one child and follows its outgoing behavior links */
    float PlaceRuntimeGraphChildAndFlowDescendants(BehaviorData &parent,
                                              BehaviorData &child,
                                              Point cursor,
                                              std::unordered_set<CK_ID> &visited);

    /** @brief Collects children reached by the behavior-link traversal */
    std::vector<BehaviorData *> CollectRuntimeGraphLinkedChildren(BehaviorData &parent,
                                                             CK_ID currentBehaviorId,
                                                             const std::unordered_set<CK_ID> &visited);

    /** @brief Collects unvisited direct children for the fallback reverse pass */
    std::vector<BehaviorData *> CollectRuntimeGraphFallbackChildren(BehaviorData &parent,
                                                               const std::unordered_set<CK_ID> &visited);

    /** @brief Sets start information for script header */
    void SetStart(BehaviorData &script, float verticalStartPos, float verticalSize);
    //==========================================================================
    // Operation and Parameter Layout
    //==========================================================================

    /** @brief Applies runtime-rebuild operation box positions */
    void ApplyParameterOperationLayout(BehaviorData &behaviorGraph, bool parentIsScriptUi);

    /** @brief Places local/shortcut sources and collapses connected local displays */
    void CalculateLocalParameterPositions(BehaviorData &behaviorGraph);

    /** @brief Gets a Script parameter display's estimated pixel bounds */
    Rect GetScriptParameterDisplayRect(const Parameter& parameter,
                                      float nameHorizontalSpacing = 0) const;

    /** @brief Collects Script display bounds, excluding records being placed */
    std::vector<Rect> CollectScriptDisplayBounds(
        const std::unordered_set<const Parameter*>& omitted,
        float nameHorizontalSpacing = 0) const;

    /** @brief Appends a graph's direct-child reservations and operation bounds, relative to origin */
    void AppendGraphElementBounds(const BehaviorData& graph,
                                  const Point& origin,
                                  std::vector<Rect>& occupied) const;

    /** @brief Gets a behavior or operation POUT display position in Script pixels */
    bool TryGetOutputParameterPosition(const LinkEndpoint& endpoint, Point& position) const;

    /** @brief Averages producer positions that share one destination display */
    static Point AveragePosition(const std::vector<Point>& positions);

    /** @brief Finds a free grid position using one display-overlap rule */
    Rect FindFreeParameterDisplay(const Rect& preferred,
                                  const std::vector<Rect>& occupied,
                                  bool searchAbove) const;

    /** @brief Places standalone local/shortcut output destinations near source POUT */
    void PlaceScriptOutputDestinations();

    /** @brief Places destination-only shortcuts in child graphs near source POUT */
    void PlaceGraphOutputDestinationShortcuts();

    /** @brief Keeps multi-consumer local sources outside dense input parameter rows */
    void PlaceScriptMultiConsumerLocalParameters();

    /** @brief Separates NAME shortcuts from Script elements and one another */
    void SeparateScriptParameterShortcutNames();

    /** @brief Calculates runtime graph operation and parameter positions */
    void CalculateRuntimeGraphElementPositions(const std::vector<CK_ID> &behaviorIds);

    /** @brief Applies fallback local parameter grid layout */
    void ApplyFallbackLocalParameterLayout(BehaviorData &behaviorGraph, bool parentIsScriptUi);

    /** @brief Moves a parameter to a position (converts pixel to grid index) */
    void MoveParameterToPosition(Parameter &parameter, const Point &position);

    /** @brief Moves an operation to a position (snaps to grid) */
    void MoveOperationToPosition(Operation &operation, const Point &position);

    /** @brief Gets input parameter position (in pixels) */
    Point GetInputParamPosition(CK_ID targetId, int inputIndex);

};
