#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/document/EditResult.h"
#include "core/schematic/ElementRef.h"

namespace vdev {

class Document;

// The Schematic Explorer's "Show in List" (Dev.exe dialog 732, the type
// mask of sub_5B5A80 / sub_5B6C20).
enum SearchKind : unsigned {
    SearchScripts = 1u << 0,
    SearchGraphs = 1u << 1,         // behaviors that are not Building Blocks
    SearchBuildingBlocks = 1u << 2,
    SearchComments = 1u << 3,
    SearchShortcuts = 1u << 4,
    SearchParameterInputs = 1u << 5,
    SearchParameterOutputs = 1u << 6,
    SearchLocalParameters = 1u << 7,
    SearchBehaviorInputs = 1u << 8,
    SearchBehaviorOutputs = 1u << 9,
    SearchOperations = 1u << 12,
    SearchBehaviors = SearchGraphs | SearchBuildingBlocks,
    SearchParameters = SearchShortcuts | SearchParameterInputs | SearchParameterOutputs | SearchLocalParameters,
    SearchAll = 0x13FFu,
};

// The Local Parameter and Shortcut Filter (sub_5B80C0): none; the
// parameters of a type (inputs, outputs, locals and shortcuts); the locals
// and shortcuts of a type whose value is value's.
enum class SearchFilter { None, Type, Value };

// Schematic Explorer query (Dev UI reference section 8.1): text matches
// names, prototype names, operation names and Comment text; a nonzero CK_ID
// matches that object exactly. Without either nothing matches unless
// listAll (Dev's list: every element of the kinds, the text and CK ID
// filters narrowing it); listAll's text is not case sensitive (Dev lowers
// both).
struct SearchQuery {
    std::string text;
    CK_ID id = 0;
    unsigned kinds = SearchAll;
    bool caseSensitive = false;
    bool listAll = false;
    // Dev's Parse only visible Items: only these Scripts (onlyScripts set).
    bool onlyScripts = false;
    std::vector<CK_ID> scripts;
    SearchFilter filter = SearchFilter::None;
    int type = -1;        // CKParameterType of Type and Value
    bool derived = false; // the types derived from type too
    CK_ID value = 0;      // a CKParameter of type holding Value's value
};

struct SearchResult {
    CK_ID script = 0;
    ElementRef ref;
    // The behavior that must be visible to show the element (its owner for
    // pins, the storing graph for other elements).
    CK_ID focus = 0;
    std::string kind;
    std::string label;
    std::string detail;
    // The CK object the element shows (the behavior, the parameter, the
    // operation; a shortcut's source), 0 for Comments.
    CK_ID object = 0;
};

// Opens every Script session of the document as needed.
std::vector<SearchResult> SearchDocument(Document &document, const SearchQuery &query);

// The Schematic Explorer's Replace Value (Dev.exe sub_5B9600): each
// parameter (Script, parameter) of value's type takes value's value; one
// undo step.
EditResult ReplaceParameterValues(Document &document, const std::vector<std::pair<CK_ID, CK_ID>> &parameters,
                                  CK_ID value);

} // namespace vdev
