#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "core/level/LevelCommands.h"
#include "core/document/Edit.h"
#include "editor/Identity.h"

namespace vdev {
class Document;

// Value records for level edits. No callbacks, Document/Context pointers,
// native object instances or native state chunks are part of this payload.
// Object references identify lifetimes in one editing session; history retains
// their replay dependencies.
// Copying a payload does not keep runtime resources alive. Reconstruction
// requires the original context and objects, not just reused numeric IDs.
struct ObjectNameChange {
    ObjectRef object{};
    std::string before, after;
};
enum class ObjectIntegerProperty { Visibility, Priority, SaveOptions, RenderPriority };
struct ObjectIntegerChange {
    ObjectRef object{};
    ObjectIntegerProperty property = ObjectIntegerProperty::Visibility;
    int before = 0, after = 0;
};
struct ParentState {
    ObjectRef parent{};
    bool background = false; // 2D only
};
inline bool operator==(const ParentState &a, const ParentState &b) { return a.parent == b.parent && a.background == b.background; }
struct ObjectParentChange {
    ObjectRef object{};
    ParentState before, after;
};
struct SceneEnvironmentState {
    SceneEnvironment value; // native reference fields are zero; use the references below
    ObjectRef backgroundTexture{}, startingCamera{};
};
bool operator==(const SceneEnvironmentState &a, const SceneEnvironmentState &b);
SceneEnvironmentState RecordSceneEnvironment(const Document &document, SceneEnvironment value);
struct SceneEnvironmentChange {
    ObjectRef object{};
    SceneEnvironmentState before, after;
};
// The tag is per batch: an integer edit does not pay for an environment or
// string payload for each object. Edit already groups different action kinds.
using LevelChanges = std::variant<std::vector<ObjectNameChange>, std::vector<ObjectIntegerChange>,
                                  std::vector<ObjectParentChange>, std::vector<SceneEnvironmentChange>>;
struct LevelActionRecord {
    DocumentToken document{};
    LevelChanges changes;
    template <typename T> std::vector<T> &Changes() { return std::get<std::vector<T>>(changes); }
    template <typename T> const std::vector<T> &Changes() const { return std::get<std::vector<T>>(changes); }
};

// Rebuild an action from owned values. Redo/Undo validate all targets before
// writing and recover partial writes from values captured before replay.
// This is an in-memory record format, not a persistent history file format.
std::unique_ptr<Action> RestoreLevelAction(LevelActionRecord record);
EditResult ApplyLevelActionRecord(Document &document, const std::string &label, LevelActionRecord record);
} // namespace vdev
