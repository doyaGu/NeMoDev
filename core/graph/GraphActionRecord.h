#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "core/document/Edit.h"

namespace vdev {

// A pin is named by its owner's lifetime and slot. Building Block callbacks
// may recreate pins; they must not make a later action address a reused ID.
struct GraphParameter {
    ObjectRef object{}, owner{};
    int slot = 0;
    bool input = true;
};
struct GraphIo {
    ObjectRef owner{};
    bool input = true;
    int slot = -1;
};
enum class GraphList {
    Behaviors,
    Links,
    Operations,
    Inputs,
    Outputs,
    Locals,
    InputIo,
    OutputIo,
    ExportedInputs,
    ExportedOutputs
};
struct GraphAttachment {
    GraphList list = GraphList::Behaviors;
    ObjectRef owner{}, object{};
    int slot = -1;
    bool insert = false;
    GraphIo in, out;
};
struct GraphSource {
    GraphParameter input;
    GraphParameter direct;
    GraphParameter shared;
};
enum class GraphDestinationMode { Add, Remove, Replace };
struct GraphDestinations {
    GraphParameter output;
    GraphDestinationMode mode = GraphDestinationMode::Replace;
    std::vector<GraphParameter> destinations;
    bool copyValue = false;
};
struct GraphText {
    ObjectRef object{};
    std::string value;
    bool parameter = false;
};
enum class GraphIntegerProperty { Priority, LinkDelay, UseTarget };
struct GraphInteger {
    ObjectRef object{};
    GraphIntegerProperty property = GraphIntegerProperty::Priority;
    int value = 0;
};
struct GraphLocalMove {
    ObjectRef from{}, to{}, parameter{};
    int fromSlot = -1, toSlot = -1;
};
using GraphOperation =
    std::variant<GraphAttachment, GraphSource, GraphDestinations, GraphText, GraphInteger, GraphLocalMove>;
struct GraphMutation {
    std::vector<GraphOperation> operations;
};
struct GraphActionRecord {
    DocumentToken document{};
    GraphMutation after, before;
};

std::unique_ptr<Action> RestoreGraphAction(GraphActionRecord record);

} // namespace vdev
