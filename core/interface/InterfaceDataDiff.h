/**
 * @file InterfaceDataDiff.h
 * @brief Structural diff tool for comparing InterfaceData instances
 *
 * This tool compares two InterfaceData instances and produces a detailed
 * field-level diff report. It's used for:
 * - Comparing reconstructed InterfaceData with saved samples
 * - Debugging which pass introduced a difference
 * - CI regression testing
 */

#pragma once

#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <map>
#include <functional>

#include "core/interface/InterfaceData.h"
#include "core/schematic/Geometry.h"

//==============================================================================
// Diff Result Structures
//==============================================================================

/**
 * @enum DiffType
 * @brief Type of difference detected
 */
enum class DiffType {
    None,           // No difference
    ValueChanged,   // Field value changed
    Added,          // Element added
    Removed,        // Element removed
    TypeMismatch,   // Element type mismatch
    CountMismatch   // Array count mismatch
};

/**
 * @struct FieldDifference
 * @brief Represents a single field difference
 */
struct FieldDifference {
    std::string path;           // Full path: "behaviors[0].rect.hPos"
    std::string elementType;    // "Behavior", "Link", "Operation", etc.
    CK_ID elementId = 0;        // ID of the element (if applicable)
    DiffType type = DiffType::None;
    std::string expected;       // Expected value (from reference)
    std::string actual;         // Actual value (from test)
    float numericDiff = 0.0f;   // Numeric difference (if applicable)
};

/**
 * @struct DiffReport
 * @brief Complete diff report between two InterfaceData instances
 */
struct DiffReport {
    bool identical = true;
    int totalDifferences = 0;

    // Differences grouped by element type
    std::vector<FieldDifference> headerDiffs;
    std::vector<FieldDifference> behaviorDiffs;
    std::vector<FieldDifference> linkDiffs;
    std::vector<FieldDifference> operationDiffs;
    std::vector<FieldDifference> parameterDiffs;
    std::vector<FieldDifference> commentDiffs;

    // Summary statistics
    std::map<std::string, int> diffsByField;  // Count by field name
    std::map<std::string, int> diffsByType;   // Count by element type

    /**
     * @brief Get all differences as a flat list
     */
    std::vector<FieldDifference> GetAllDifferences() const {
        std::vector<FieldDifference> all;
        all.insert(all.end(), headerDiffs.begin(), headerDiffs.end());
        all.insert(all.end(), behaviorDiffs.begin(), behaviorDiffs.end());
        all.insert(all.end(), linkDiffs.begin(), linkDiffs.end());
        all.insert(all.end(), operationDiffs.begin(), operationDiffs.end());
        all.insert(all.end(), parameterDiffs.begin(), parameterDiffs.end());
        all.insert(all.end(), commentDiffs.begin(), commentDiffs.end());
        return all;
    }

    /**
     * @brief Generate a human-readable summary
     */
    std::string GetSummary() const {
        std::ostringstream ss;
        ss << "InterfaceData Diff Report\n";
        ss << "=========================\n";
        ss << "Status: " << (identical ? "IDENTICAL" : "DIFFERENT") << "\n";
        ss << "Total differences: " << totalDifferences << "\n\n";

        if (!identical) {
            ss << "By element type:\n";
            for (const auto& kv : diffsByType) {
                ss << "  " << kv.first << ": " << kv.second << "\n";
            }
            ss << "\nBy field:\n";
            for (const auto& kv : diffsByField) {
                ss << "  " << kv.first << ": " << kv.second << "\n";
            }
        }

        return ss.str();
    }

    /**
     * @brief Generate detailed diff output
     */
    std::string GetDetailedReport() const {
        std::ostringstream ss;
        ss << GetSummary();

        if (!identical) {
            ss << "\nDetailed Differences:\n";
            ss << "--------------------\n";

            auto printDiffs = [&ss](const std::vector<FieldDifference>& diffs,
                                    const char* section) {
                if (!diffs.empty()) {
                    ss << "\n" << section << " (" << diffs.size() << "):\n";
                    for (const auto& d : diffs) {
                        ss << "  " << d.path << ": "
                           << d.expected << " -> " << d.actual;
                        if (d.numericDiff != 0.0f) {
                            ss << " (diff: " << d.numericDiff << ")";
                        }
                        ss << "\n";
                    }
                }
            };

            printDiffs(headerDiffs, "Header");
            printDiffs(behaviorDiffs, "Behaviors");
            printDiffs(linkDiffs, "Links");
            printDiffs(operationDiffs, "Operations");
            printDiffs(parameterDiffs, "Parameters");
            printDiffs(commentDiffs, "Comments");
        }

        return ss.str();
    }
};

//==============================================================================
// InterfaceDataDiff Class
//==============================================================================

/**
 * @class InterfaceDataDiff
 * @brief Compares two InterfaceData instances and produces a diff report
 */
class InterfaceDataDiff {
public:
    /**
     * @brief Configuration for diff comparison
     */
    struct Config {
        float floatTolerance = 0.001f;    // Tolerance for float comparisons
        bool compareOrder = true;          // Compare element ordering
        bool compareLinkPoints = true;     // Compare link control points
        bool ignoreDefaults = false;       // Ignore fields with default values
        bool ignoreSyntheticParameterIds = false; // Ignore parsed UI parameter id=0 vs runtime object id
        bool compareShortcutEndpointsBySourceId = false; // Treat type-5 slot reorder as non-semantic
        bool compareParameterOperationLinksByExpandedForm = false; // Treat op-id link and split op links as equivalent
        bool compareLocalShortcutEndpointsBySourceId = false; // Treat PLOCAL and type-5 shortcut as same local source
    };

    /**
     * @brief Constructor
     * @param config Diff configuration
     */
    InterfaceDataDiff() = default;
    explicit InterfaceDataDiff(const Config& config) : m_Config(config) {}

    /**
     * @brief Compare two InterfaceData instances
     * @param expected Reference/expected data
     * @param actual Test/actual data
     * @return Diff report
     */
    DiffReport Compare(const InterfaceData& expected, const InterfaceData& actual) {
        m_Report = DiffReport();

        // Compare version
        CompareField("version", expected.version, actual.version);

        // Compare header
        CompareHeader(expected.header, actual.header);

        // Compare root behavior
        CompareBehavior("rootBehavior", expected.rootBehavior, actual.rootBehavior);

        // Compare behaviors array
        CompareBehaviorArray(expected.behaviors, actual.behaviors);

        // Update summary
        m_Report.identical = (m_Report.totalDifferences == 0);

        return m_Report;
    }

private:
    //--------------------------------------------------------------------------
    // Comparison Helpers
    //--------------------------------------------------------------------------

    void CompareField(const std::string& path, int expected, int actual) {
        if (expected != actual) {
            AddDiff(path, "Header", 0, DiffType::ValueChanged,
                    std::to_string(expected), std::to_string(actual));
        }
    }

    void CompareField(const std::string& path, unsigned int expected, unsigned int actual) {
        if (expected != actual) {
            AddDiff(path, "Header", 0, DiffType::ValueChanged,
                    std::to_string(expected), std::to_string(actual));
        }
    }

    void CompareField(const std::string& path, unsigned long expected, unsigned long actual) {
        if (expected != actual) {
            AddDiff(path, "Header", 0, DiffType::ValueChanged,
                    std::to_string(expected), std::to_string(actual));
        }
    }

    void CompareField(const std::string& path, const std::string& elementType,
                      CK_ID id, float expected, float actual) {
        if (!FloatEqual(expected, actual)) {
            AddDiff(path, elementType, id, DiffType::ValueChanged,
                    FloatToString(expected), FloatToString(actual),
                    actual - expected);
        }
    }

    void CompareHeader(const Header& expected, const Header& actual) {
        const std::string prefix = "header.";

        CompareField(prefix + "hStartPos", "Header", 0, expected.hStartPos, actual.hStartPos);
        CompareField(prefix + "vStartPos", "Header", 0, expected.vStartPos, actual.vStartPos);
        CompareField(prefix + "vStart", "Header", 0, expected.vStart, actual.vStart);
        CompareField(prefix + "hStartSize", "Header", 0, expected.hStartSize, actual.hStartSize);
        CompareField(prefix + "vStartSize", "Header", 0, expected.vStartSize, actual.vStartSize);
        CompareField(prefix + "vSize", "Header", 0, expected.vSize, actual.vSize);

        if (expected.color != actual.color) {
            AddDiff(prefix + "color", "Header", 0, DiffType::ValueChanged,
                    ColorToString(expected.color), ColorToString(actual.color));
        }
    }

    void CompareBehavior(const std::string& prefix, const BehaviorData& expected,
                         const BehaviorData& actual) {
        // Compare basic fields
        if (expected.id != actual.id) {
            AddDiff(prefix + ".id", "Behavior", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.id), std::to_string(actual.id));
        }

        // Compare rect
        CompareRect(prefix + ".rect", expected.id, expected.rect, actual.rect);

        // Compare flags
        if (expected.folded != actual.folded) {
            AddDiff(prefix + ".folded", "Behavior", expected.id, DiffType::ValueChanged,
                    expected.folded ? "true" : "false", actual.folded ? "true" : "false");
        }

        // Compare links
        CompareLinkArray(prefix, expected, actual);

        // Compare operations
        CompareOperationArray(prefix, expected.id, expected.operations, actual.operations);

        // Compare local parameters
        CompareParameterArray(prefix + ".localParams", expected.id,
                              expected.localParams, actual.localParams);

        // Compare shared parameters
        CompareParameterArray(prefix + ".sharedParams", expected.id,
                              expected.sharedParams, actual.sharedParams);

        // Compare comments
        CompareCommentArray(prefix, expected.id, expected.comments, actual.comments);
    }

    void CompareBehaviorArray(const std::vector<BehaviorData>& expected,
                              const std::vector<BehaviorData>& actual) {
        if (expected.size() != actual.size()) {
            AddDiff("behaviorCount", "Behavior", 0, DiffType::CountMismatch,
                    std::to_string(expected.size()), std::to_string(actual.size()));
        }

        if (m_Config.compareOrder) {
            size_t minSize = std::min(expected.size(), actual.size());
            for (size_t i = 0; i < minSize; ++i) {
                std::ostringstream prefix;
                prefix << "behaviors[" << i << "]";
                CompareBehavior(prefix.str(), expected[i], actual[i]);
            }
            return;
        }

        std::vector<bool> matched(actual.size(), false);
        for (size_t i = 0; i < expected.size(); ++i) {
            const BehaviorData& expectedBehavior = expected[i];
            size_t match = actual.size();
            for (size_t j = 0; j < actual.size(); ++j) {
                if (!matched[j] && actual[j].id == expectedBehavior.id) {
                    match = j;
                    break;
                }
            }

            std::ostringstream prefix;
            prefix << "behaviors[id=" << expectedBehavior.id << "]";
            if (match == actual.size()) {
                AddDiff(prefix.str(), "Behavior", expectedBehavior.id, DiffType::Removed,
                        "present", "missing");
                continue;
            }

            matched[match] = true;
            CompareBehavior(prefix.str(), expectedBehavior, actual[match]);
        }

        for (size_t j = 0; j < actual.size(); ++j) {
            if (!matched[j]) {
                std::ostringstream prefix;
                prefix << "behaviors[id=" << actual[j].id << "]";
                AddDiff(prefix.str(), "Behavior", actual[j].id, DiffType::Added,
                        "missing", "present");
            }
        }
    }

    void CompareRect(const std::string& prefix, CK_ID id,
                     const Rect& expected, const Rect& actual) {
        CompareField(prefix + ".hPos", "Behavior", id, expected.hPos, actual.hPos);
        CompareField(prefix + ".vPos", "Behavior", id, expected.vPos, actual.vPos);
        CompareField(prefix + ".hSize", "Behavior", id, expected.hSize, actual.hSize);
        CompareField(prefix + ".vSize", "Behavior", id, expected.vSize, actual.vSize);
    }

    void CompareLinkArray(const std::string& prefix,
                          const BehaviorData& expectedBehavior,
                          const BehaviorData& actualBehavior) {
        const CK_ID behaviorId = expectedBehavior.id;
        const std::vector<Link>& expected = expectedBehavior.links;
        const std::vector<Link>& actual = actualBehavior.links;

        const bool deferCountForExpandedLinks =
            !m_Config.compareOrder && m_Config.compareParameterOperationLinksByExpandedForm;
        if (expected.size() != actual.size() && !deferCountForExpandedLinks) {
            AddDiff(prefix + ".linkCount", "Link", behaviorId, DiffType::CountMismatch,
                    std::to_string(expected.size()), std::to_string(actual.size()));
        }

        if (m_Config.compareOrder) {
            size_t minSize = std::min(expected.size(), actual.size());
            for (size_t i = 0; i < minSize; ++i) {
                std::ostringstream linkPrefix;
                linkPrefix << prefix << ".links[" << i << "]";
                CompareLink(linkPrefix.str(), expectedBehavior, actualBehavior, expected[i], actual[i]);
            }
            return;
        }

        std::vector<bool> matched(actual.size(), false);
        bool hasUnmatchedExpected = false;
        for (size_t i = 0; i < expected.size(); ++i) {
            const Link& expectedLink = expected[i];
            size_t match = actual.size();
            for (size_t j = 0; j < actual.size(); ++j) {
                if (!matched[j] && LinksMatchForOrderlessCompare(
                        expectedBehavior, actualBehavior, expectedLink, actual[j])) {
                    match = j;
                    break;
                }
            }

            std::ostringstream linkPrefix;
            linkPrefix << prefix << ".links[" << LinkLabel(expectedLink) << "]";
            if (match == actual.size()) {
                std::pair<size_t, size_t> expandedMatch;
                if (FindExpandedParameterOperationLinkMatch(
                        expectedBehavior, actualBehavior, expectedLink, actual, matched, expandedMatch)) {
                    matched[expandedMatch.first] = true;
                    matched[expandedMatch.second] = true;
                    continue;
                }
                AddDiff(linkPrefix.str(), "Link", expectedLink.id, DiffType::Removed,
                        "present", "missing");
                hasUnmatchedExpected = true;
                continue;
            }

            matched[match] = true;
            CompareLink(linkPrefix.str(), expectedBehavior, actualBehavior, expectedLink, actual[match]);
        }

        for (size_t j = 0; j < actual.size(); ++j) {
            if (!matched[j]) {
                std::ostringstream linkPrefix;
                linkPrefix << prefix << ".links[" << LinkLabel(actual[j]) << "]";
                AddDiff(linkPrefix.str(), "Link", actual[j].id, DiffType::Added,
                        "missing", "present");
                hasUnmatchedExpected = true;
            }
        }

        if (deferCountForExpandedLinks && expected.size() != actual.size() && hasUnmatchedExpected) {
            AddDiff(prefix + ".linkCount", "Link", behaviorId, DiffType::CountMismatch,
                    std::to_string(expected.size()), std::to_string(actual.size()));
        }
    }

    bool FindExpandedParameterOperationLinkMatch(
        const BehaviorData& expectedBehavior,
        const BehaviorData& actualBehavior,
        const Link& expectedLink,
        const std::vector<Link>& actual,
        const std::vector<bool>& matched,
        std::pair<size_t, size_t>& expandedMatch) const {

        if (!m_Config.compareParameterOperationLinksByExpandedForm ||
            expectedLink.id == 0 ||
            expectedLink.start.hasLegacyTypeCode || expectedLink.end.hasLegacyTypeCode ||
            expectedLink.type != LINK_TYPE_PARAMETER) {
            return false;
        }

        bool operationExists = false;
        for (const auto& operation : actualBehavior.operations) {
            if (operation.id == expectedLink.id) {
                operationExists = true;
                break;
            }
        }
        if (!operationExists) {
            return false;
        }

        for (size_t i = 0; i < actual.size(); ++i) {
            if (matched[i]) {
                continue;
            }
            const Link& inbound = actual[i];
            if (inbound.type != LINK_TYPE_PARAMETER ||
                inbound.end.hasLegacyTypeCode ||
                inbound.end.id != expectedLink.id ||
                inbound.end.type != ENDPOINT_PIN ||
                !EndpointsEqual(expectedBehavior, actualBehavior, expectedLink.start, inbound.start)) {
                continue;
            }

            for (size_t j = 0; j < actual.size(); ++j) {
                if (i == j || matched[j]) {
                    continue;
                }
                const Link& outbound = actual[j];
                if (outbound.type == LINK_TYPE_PARAMETER &&
                    !outbound.start.hasLegacyTypeCode &&
                    outbound.start.id == expectedLink.id &&
                    outbound.start.type == ENDPOINT_POUT &&
                    EndpointsEqual(expectedBehavior, actualBehavior, expectedLink.end, outbound.end)) {
                    expandedMatch = {i, j};
                    return true;
                }
            }
        }

        return false;
    }

    bool LinksMatchForOrderlessCompare(const BehaviorData& expectedBehavior,
                                       const BehaviorData& actualBehavior,
                                       const Link& expected,
                                       const Link& actual) const {
        if (expected.id != 0 || actual.id != 0) {
            return expected.id == actual.id;
        }

        return expected.type == actual.type &&
               expected.highlight == actual.highlight &&
               EndpointsEqual(expectedBehavior, actualBehavior, expected.start, actual.start) &&
               EndpointsEqual(expectedBehavior, actualBehavior, expected.end, actual.end);
    }

    bool EndpointsEqual(const BehaviorData& expectedBehavior,
                        const BehaviorData& actualBehavior,
                        const LinkEndpoint& a,
                        const LinkEndpoint& b) const {
        if (a.hasLegacyTypeCode || b.hasLegacyTypeCode) {
            return a.hasLegacyTypeCode == b.hasLegacyTypeCode &&
                   a.id == b.id && a.index == b.index && a.type == b.type;
        }
        if (ShortcutEndpointsReferToSameSource(expectedBehavior, actualBehavior, a, b)) {
            return true;
        }
        if (LocalShortcutEndpointsReferToSameSource(expectedBehavior, actualBehavior, a, b)) {
            return true;
        }
        return a.id == b.id && a.index == b.index && a.type == b.type;
    }

    bool ShortcutEndpointsReferToSameSource(const BehaviorData& expectedBehavior,
                                            const BehaviorData& actualBehavior,
                                            const LinkEndpoint& expected,
                                            const LinkEndpoint& actual) const {
        if (!m_Config.compareShortcutEndpointsBySourceId) {
            return false;
        }
        if (expected.hasLegacyTypeCode || actual.hasLegacyTypeCode) return false;
        if (expected.id != actual.id ||
            expected.type != ENDPOINT_POUT_SHORTCUT ||
            actual.type != ENDPOINT_POUT_SHORTCUT) {
            return false;
        }
        if (expected.index < 0 || actual.index < 0 ||
            expected.index >= static_cast<int>(expectedBehavior.sharedParams.size()) ||
            actual.index >= static_cast<int>(actualBehavior.sharedParams.size())) {
            return false;
        }
        return expectedBehavior.sharedParams[expected.index].sourceId ==
               actualBehavior.sharedParams[actual.index].sourceId;
    }

    bool LocalShortcutEndpointsReferToSameSource(const BehaviorData& expectedBehavior,
                                                 const BehaviorData& actualBehavior,
                                                 const LinkEndpoint& expected,
                                                 const LinkEndpoint& actual) const {
        if (!m_Config.compareLocalShortcutEndpointsBySourceId || expected.id != actual.id) {
            return false;
        }
        if (expected.hasLegacyTypeCode || actual.hasLegacyTypeCode) return false;

        if (expected.type == ENDPOINT_POUT_SHORTCUT && actual.type == ENDPOINT_PLOCAL) {
            if (expected.index < 0 || actual.index < 0 ||
                expected.index >= static_cast<int>(expectedBehavior.sharedParams.size()) ||
                actual.index >= static_cast<int>(actualBehavior.localParams.size())) {
                return false;
            }
            return expectedBehavior.sharedParams[expected.index].sourceId != 0 &&
                   expectedBehavior.sharedParams[expected.index].sourceId ==
                       actualBehavior.localParams[actual.index].id;
        }

        if (expected.type == ENDPOINT_PLOCAL && actual.type == ENDPOINT_POUT_SHORTCUT) {
            if (expected.index < 0 || actual.index < 0 ||
                expected.index >= static_cast<int>(actualBehavior.localParams.size()) ||
                actual.index >= static_cast<int>(actualBehavior.sharedParams.size())) {
                return false;
            }

            CK_ID expectedLocalSource = 0;
            if (expected.index < static_cast<int>(expectedBehavior.localParams.size())) {
                expectedLocalSource = expectedBehavior.localParams[expected.index].id;
            }
            if (expectedLocalSource == 0) {
                expectedLocalSource = actualBehavior.localParams[expected.index].id;
            }

            return expectedLocalSource != 0 &&
                   expectedLocalSource == actualBehavior.sharedParams[actual.index].sourceId;
        }

        return false;
    }

    std::string LinkLabel(const Link& link) const {
        std::ostringstream out;
        if (link.id != 0) {
            out << "id=" << link.id << ",";
        }
        out << "type=" << static_cast<int>(link.type)
            << ",start=" << link.start.id << ":" << link.start.index << ":" << static_cast<int>(link.start.type)
            << ",end=" << link.end.id << ":" << link.end.index << ":" << static_cast<int>(link.end.type);
        if (link.start.hasLegacyTypeCode || link.end.hasLegacyTypeCode)
            out << ",legacyTypeCode=" << link.start.hasLegacyTypeCode << ":" << link.end.hasLegacyTypeCode;
        return out.str();
    }

    void CompareLink(const std::string& prefix,
                     const BehaviorData& expectedBehavior,
                     const BehaviorData& actualBehavior,
                     const Link& expected,
                     const Link& actual) {
        if (expected.id != actual.id) {
            AddDiff(prefix + ".id", "Link", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.id), std::to_string(actual.id));
        }

        if (expected.type != actual.type) {
            AddDiff(prefix + ".type", "Link", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.type), std::to_string(actual.type));
        }

        if (expected.highlight != actual.highlight) {
            AddDiff(prefix + ".highlight", "Link", expected.id, DiffType::ValueChanged,
                    expected.highlight ? "true" : "false",
                    actual.highlight ? "true" : "false");
        }

        CompareEndpoint(prefix + ".start", expected.id, expectedBehavior, actualBehavior,
                        expected.start, actual.start);
        CompareEndpoint(prefix + ".end", expected.id, expectedBehavior, actualBehavior,
                        expected.end, actual.end);

        // Compare control points
        if (m_Config.compareLinkPoints) {
            if (expected.points.size() != actual.points.size()) {
                AddDiff(prefix + ".pointCount", "Link", expected.id, DiffType::CountMismatch,
                        std::to_string(expected.points.size()),
                        std::to_string(actual.points.size()));
            }

            size_t minPoints = std::min(expected.points.size(), actual.points.size());
            for (size_t i = 0; i < minPoints; ++i) {
                std::ostringstream ptPrefix;
                ptPrefix << prefix << ".points[" << i << "]";

                CompareField(ptPrefix.str() + ".h", "Link", expected.id,
                            expected.points[i].h, actual.points[i].h);
                CompareField(ptPrefix.str() + ".v", "Link", expected.id,
                            expected.points[i].v, actual.points[i].v);
            }
        }
    }

    void CompareEndpoint(const std::string& prefix, CK_ID linkId,
                         const BehaviorData& expectedBehavior,
                         const BehaviorData& actualBehavior,
                         const LinkEndpoint& expected, const LinkEndpoint& actual) {
        if (EndpointsEqual(expectedBehavior, actualBehavior, expected, actual)) {
            return;
        }

        if (expected.id != actual.id) {
            AddDiff(prefix + ".id", "Link", linkId, DiffType::ValueChanged,
                    std::to_string(expected.id), std::to_string(actual.id));
        }

        if (expected.index != actual.index &&
            !ShortcutEndpointsReferToSameSource(expectedBehavior, actualBehavior, expected, actual)) {
            AddDiff(prefix + ".index", "Link", linkId, DiffType::ValueChanged,
                    std::to_string(expected.index), std::to_string(actual.index),
                    static_cast<float>(actual.index - expected.index));
        }

        if (expected.type != actual.type) {
            AddDiff(prefix + ".type", "Link", linkId, DiffType::ValueChanged,
                    std::to_string(expected.type), std::to_string(actual.type));
        }
        if (expected.hasLegacyTypeCode != actual.hasLegacyTypeCode) {
            AddDiff(prefix + ".hasLegacyTypeCode", "Link", linkId, DiffType::ValueChanged,
                    std::to_string(expected.hasLegacyTypeCode), std::to_string(actual.hasLegacyTypeCode));
        }
    }

    void CompareOperationArray(const std::string& prefix, CK_ID behaviorId,
                               const std::vector<Operation>& expected,
                               const std::vector<Operation>& actual) {
        if (expected.size() != actual.size()) {
            AddDiff(prefix + ".opCount", "Operation", behaviorId, DiffType::CountMismatch,
                    std::to_string(expected.size()), std::to_string(actual.size()));
        }

        if (m_Config.compareOrder) {
            size_t minSize = std::min(expected.size(), actual.size());
            for (size_t i = 0; i < minSize; ++i) {
                std::ostringstream opPrefix;
                opPrefix << prefix << ".ops[" << i << "]";
                CompareOperation(opPrefix.str(), expected[i], actual[i]);
            }
            return;
        }

        std::vector<bool> matched(actual.size(), false);
        for (size_t i = 0; i < expected.size(); ++i) {
            const Operation& expectedOperation = expected[i];
            size_t match = actual.size();
            for (size_t j = 0; j < actual.size(); ++j) {
                if (!matched[j] && actual[j].id == expectedOperation.id) {
                    match = j;
                    break;
                }
            }

            std::ostringstream opPrefix;
            opPrefix << prefix << ".ops[id=" << expectedOperation.id << "]";
            if (match == actual.size()) {
                AddDiff(opPrefix.str(), "Operation", expectedOperation.id, DiffType::Removed,
                        "present", "missing");
                continue;
            }

            matched[match] = true;
            CompareOperation(opPrefix.str(), expectedOperation, actual[match]);
        }

        for (size_t j = 0; j < actual.size(); ++j) {
            if (!matched[j]) {
                std::ostringstream opPrefix;
                opPrefix << prefix << ".ops[id=" << actual[j].id << "]";
                AddDiff(opPrefix.str(), "Operation", actual[j].id, DiffType::Added,
                        "missing", "present");
            }
        }
    }

    void CompareOperation(const std::string& prefix, const Operation& expected,
                          const Operation& actual) {
        if (expected.id != actual.id) {
            AddDiff(prefix + ".id", "Operation", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.id), std::to_string(actual.id));
        }

        CompareField(prefix + ".hPos", "Operation", expected.id, expected.hPos, actual.hPos);
        CompareField(prefix + ".vPos", "Operation", expected.id, expected.vPos, actual.vPos);
    }

    void CompareParameterArray(const std::string& prefix, CK_ID behaviorId,
                               const std::vector<Parameter>& expected,
                               const std::vector<Parameter>& actual) {
        if (expected.size() != actual.size()) {
            AddDiff(prefix + ".count", "Parameter", behaviorId, DiffType::CountMismatch,
                    std::to_string(expected.size()), std::to_string(actual.size()));
        }

        size_t minSize = std::min(expected.size(), actual.size());
        for (size_t i = 0; i < minSize; ++i) {
            std::ostringstream paramPrefix;
            paramPrefix << prefix << "[" << i << "]";
            CompareParameter(paramPrefix.str(), expected[i], actual[i]);
        }
    }

    void CompareParameter(const std::string& prefix, const Parameter& expected,
                          const Parameter& actual) {
        const bool ignoreSyntheticId = m_Config.ignoreSyntheticParameterIds &&
                                       (expected.id == 0 || actual.id == 0);
        if (!ignoreSyntheticId && expected.id != actual.id) {
            AddDiff(prefix + ".id", "Parameter", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.id), std::to_string(actual.id));
        }

        // Note: Parameter positions are GRID INDICES, not pixels!
        if (expected.hPos != actual.hPos) {
            AddDiff(prefix + ".hPos", "Parameter", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.hPos), std::to_string(actual.hPos),
                    static_cast<float>(actual.hPos - expected.hPos));
        }

        if (expected.vPos != actual.vPos) {
            AddDiff(prefix + ".vPos", "Parameter", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.vPos), std::to_string(actual.vPos),
                    static_cast<float>(actual.vPos - expected.vPos));
        }

        if (expected.style != actual.style) {
            AddDiff(prefix + ".style", "Parameter", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.style), std::to_string(actual.style));
        }

        if (expected.sourceId != actual.sourceId) {
            AddDiff(prefix + ".sourceId", "Parameter", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.sourceId), std::to_string(actual.sourceId));
        }
    }

    void CompareCommentArray(const std::string& prefix, CK_ID behaviorId,
                             const std::vector<Comment>& expected,
                             const std::vector<Comment>& actual) {
        if (expected.size() != actual.size()) {
            AddDiff(prefix + ".commentCount", "Comment", behaviorId, DiffType::CountMismatch,
                    std::to_string(expected.size()), std::to_string(actual.size()));
        }

        size_t minSize = std::min(expected.size(), actual.size());
        for (size_t i = 0; i < minSize; ++i) {
            std::ostringstream commentPrefix;
            commentPrefix << prefix << ".comments[" << i << "]";
            CompareComment(commentPrefix.str(), expected[i], actual[i]);
        }
    }

    void CompareComment(const std::string& prefix, const Comment& expected,
                        const Comment& actual) {
        CompareField(prefix + ".hPos", "Comment", expected.id, expected.hPos, actual.hPos);
        CompareField(prefix + ".vPos", "Comment", expected.id, expected.vPos, actual.vPos);
        CompareField(prefix + ".width", "Comment", expected.id, expected.width, actual.width);
        CompareField(prefix + ".height", "Comment", expected.id, expected.height, actual.height);

        if (expected.text != actual.text) {
            AddDiff(prefix + ".text", "Comment", expected.id, DiffType::ValueChanged,
                    expected.text, actual.text);
        }

        if (expected.styleFlags != actual.styleFlags) {
            AddDiff(prefix + ".styleFlags", "Comment", expected.id, DiffType::ValueChanged,
                    std::to_string(expected.styleFlags), std::to_string(actual.styleFlags));
        }
    }

    //--------------------------------------------------------------------------
    // Helper Methods
    //--------------------------------------------------------------------------

    bool FloatEqual(float a, float b) const {
        return std::abs(a - b) < m_Config.floatTolerance;
    }

    std::string FloatToString(float value) const {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << value;
        return ss.str();
    }

    std::string ColorToString(unsigned int color) const {
        std::ostringstream ss;
        ss << "0x" << std::hex << std::setfill('0') << std::setw(6) << color;
        return ss.str();
    }

    void AddDiff(const std::string& path, const std::string& elementType, CK_ID id,
                 DiffType type, const std::string& expected, const std::string& actual,
                 float numericDiff = 0.0f) {
        FieldDifference diff;
        diff.path = path;
        diff.elementType = elementType;
        diff.elementId = id;
        diff.type = type;
        diff.expected = expected;
        diff.actual = actual;
        diff.numericDiff = numericDiff;

        // Add to appropriate category
        if (elementType == "Header") {
            m_Report.headerDiffs.push_back(diff);
        } else if (elementType == "Behavior") {
            m_Report.behaviorDiffs.push_back(diff);
        } else if (elementType == "Link") {
            m_Report.linkDiffs.push_back(diff);
        } else if (elementType == "Operation") {
            m_Report.operationDiffs.push_back(diff);
        } else if (elementType == "Parameter") {
            m_Report.parameterDiffs.push_back(diff);
        } else if (elementType == "Comment") {
            m_Report.commentDiffs.push_back(diff);
        }

        m_Report.totalDifferences++;
        m_Report.diffsByType[elementType]++;

        // Extract field name from path
        size_t lastDot = path.rfind('.');
        std::string fieldName = (lastDot != std::string::npos) ?
                               path.substr(lastDot + 1) : path;
        m_Report.diffsByField[fieldName]++;
    }

    Config m_Config;
    DiffReport m_Report;
};
