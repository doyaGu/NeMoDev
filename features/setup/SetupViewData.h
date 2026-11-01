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
    uint32_t classId = 0;
};
struct SetupFont {
    std::string face;
    int size = 12, weight = 400;
    bool italic = false, underline = false;
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

// Views borrow fields from a copied page description, never from the runtime.
class SetupFields {
public:
    SetupFields() = default;
    explicit SetupFields(const std::vector<EditorField> &fields) : m_Fields(&fields) {}
    const EditorField *Find(const char *key) const;
    bool Has(const char *key) const { return Find(key) != nullptr; }
    int64_t Integer(const char *key) const;
    bool Boolean(const char *key) const { return Integer(key) != 0; }
    double Real(const char *key) const;
    std::string Text(const char *key) const;
    ObjectRef Object(const char *key) const;
    const std::vector<ObjectRef> &Objects(const char *key) const;
    const std::vector<uint64_t> &Integers(const char *key) const;
    const std::vector<double> &Reals(const char *key) const;
    const std::vector<std::string> &Texts(const char *key) const;
    const std::vector<EditorField> &Rows(const char *key) const;
    SetupFields Record(const char *key) const { return SetupFields(Rows(key)); }

private:
    const std::vector<EditorField> *m_Fields = nullptr;
};

struct SetupDetails {
    ObjectRef target{};
    uint64_t documentRevision = 0;
    std::vector<EditorField> fields;
    SetupFields Values() const { return SetupFields(fields); }
};

bool ReadSetupDetails(const EditorClient &, ObjectRef, const char *section, SetupDetails &,
                      const std::vector<uint64_t> &indices = {});
bool ReadSetupProperties(const EditorClient &, ObjectRef, const std::vector<const char *> &, SetupDetails &,
                         const std::vector<uint64_t> &indices = {});
bool ReadSetupChoices(const EditorClient &, uint32_t classId, std::vector<SetupObjectRow> &);

} // namespace vdev
