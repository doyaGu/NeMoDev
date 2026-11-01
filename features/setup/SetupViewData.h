#pragma once

#include "workspace/EditorClient.h"
#include <array>

namespace vdev {

enum class SetupKind {
    Entity3D,
    Camera,
    Light,
    Entity2D,
    Material,
    Mesh,
    Texture,
    Grid,
    Other,
    Curve,
    Character,
    Sound,
    Array,
    Group,
    Scene,
    Sprite3D,
    Sprite2D,
    Animation,
    ObjectAnimation,
    MidiSound,
    SpriteText,
    KinematicChain,
    Layer,
    Parameter,
    ParameterIn,
    Operation,
    Behavior,
    BehaviorIO,
    BehaviorLink,
    Synchro,
    State,
    CriticalSection,
    InterfaceObjects
};
enum class SetupFileKind { None, Texture, Sprite, Wave, Midi };

struct SetupPageData {
    ObjectRef target{}, point{}, grid{};
    SetupKind kind = SetupKind::Other;
    SetupFileKind fileKind = SetupFileKind::None;
    uint32_t classId = 0;
    int layer = -1;
    std::string title, name, filename;
    bool behavioral = false, attributeDrop = false, frame = false;
    bool sceneScoped = false, currentScene = false, inCurrentScene = false;
    bool initialVisible = false, initialHere = false, initialValue = false;
};
struct SetupObjectRow {
    ObjectRef object{};
    std::string name;
};
struct SetupTransformData {
    std::array<float, 16> world{}, local{};
    std::array<float, 6> box{};
};

// A failed or malformed read leaves the caller's copy unchanged.
bool ReadSetupTarget(const EditorClient &, ObjectRef requested, ObjectRef &target, int &point);
bool ReadSetupPage(const EditorClient &, ObjectRef target, int point, SetupPageData &);
bool ReadSetupObjects(const EditorClient &, ObjectRef target, bool currentScene, std::vector<SetupObjectRow> &);
bool ReadSetupTransform(const EditorClient &, ObjectRef target, SetupTransformData &);

} // namespace vdev
