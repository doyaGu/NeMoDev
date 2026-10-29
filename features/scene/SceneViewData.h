#pragma once
#include "features/scene/SceneModel.h"
#include "workspace/EditorClient.h"
#include <set>
namespace vdev {
inline std::vector<ObjectRef> SceneObjects(const EditorResponse &r, const char *key) {
    const auto *f = EditorClient::Find(r, key);
    return f ? f->objects : std::vector<ObjectRef>{};
}
inline std::vector<uint64_t> SceneIntegers(const EditorResponse &r, const char *key) {
    const auto *f = EditorClient::Find(r, key);
    return f ? f->integers : std::vector<uint64_t>{};
}
inline std::vector<std::string> SceneTexts(const EditorResponse &r, const char *key) {
    const auto *f = EditorClient::Find(r, key);
    return f ? f->texts : std::vector<std::string>{};
}
inline ObjectRef SceneObject(const EditorResponse &r, const char *key) {
    const auto *f = EditorClient::Find(r, key);
    return f ? f->object : ObjectRef{};
}
EditorStatus QuerySceneTree(const EditorClient &, const SceneTreeOptions &, bool hierarchy, SceneTree &);
struct SceneAttributeType {
    uint64_t id = 0, flags = 0, category = 0;
    std::string name, parameterName, compatibleName;
};
struct SceneAttributeCategory {
    std::string name;
    uint64_t flags = 0;
    std::vector<uint64_t> types;
};
struct SceneAttributeCatalog {
    bool available = false;
    uint64_t version = 0;
    std::vector<SceneAttributeType> types;
    std::vector<SceneAttributeCategory> categories;
    const SceneAttributeType *Find(uint64_t id) const {
        for (const auto &type : types) {
            if (type.id == id) {
                return &type;
            }
        }
        return nullptr;
    }
};
SceneAttributeCatalog QuerySceneAttributes(const EditorClient &, const std::vector<ObjectRef> & = {});
struct SceneAttributeRow {
    ObjectRef object{}, parameter{};
    uint64_t type = 0;
    std::string name, value, parameterName;
};
std::vector<SceneAttributeRow> QueryAttributeObjects(const EditorClient &, const SceneAttributeCatalog &,
                                                     const std::set<uint64_t> &, const std::vector<ObjectRef> & = {});
// A tree retains non-render rows locally. Only externally changed selection
// revisions replace them; completion versions distinguish its own deferred click.
class SceneSelection {
  public:
    std::set<ObjectRef> objects;
    bool locked = false;
    void Pull(const EditorClient &);
    void Push(const EditorClient &);
    void Reset() {
        objects.clear();
        m_Last.clear();
        m_Seen = 0;
        m_Operation = 0;
    }

  private:
    uint64_t m_Seen = 0;
    EditorOperation m_Operation = 0;
    std::set<ObjectRef> m_Last;
};
} // namespace vdev
