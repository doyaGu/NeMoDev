#pragma once
#include "application/EditorApplication.h"
#include <string>
#include <vector>

namespace vdev {
class EditorClient;
struct LayoutScriptRow { ObjectRef object{}; std::string name; };
struct LayoutObjectRow {
    ObjectRef object{};
    uint32_t cid = 0;
    bool view = false;
    std::string name, className, kind;
    std::vector<LayoutScriptRow> scripts;
    std::vector<ObjectRef> members;
};
struct LayoutSceneData {
    ObjectRef scene{}, camera{}, savedCamera{}, frame{}, initialTarget{}, matchingGroup{};
    bool initial = false;
    uint32_t selectedTypes = 0;
    std::vector<LayoutObjectRow> rows;
    std::vector<std::string> frameShapes;
    const LayoutObjectRow *Find(uint32_t id) const;
private:
    DocumentToken m_Document{};
    uint64_t m_SourceVersion = 0, m_SelectionVersion = 0;
    std::string m_Section;
    std::vector<EditorField> m_Arguments;
    friend bool ReadLayoutScene(const EditorClient &, const char *, LayoutSceneData &, const std::vector<EditorValue> &);
};
// Tool settings keep the picked referent's lifetime, rather than a native slot.
struct LayoutTransformSettings {
    int axes = 5, referential = 1, pivot = 1;
    ObjectRef referent{};
    bool hierarchy = true, snap = false;
};
struct LayoutSelectionData {
    std::vector<ObjectRef> objects;
    bool locked = false;
    uint64_t revision = 0;
    bool Contains(uint32_t id) const;
private:
    DocumentToken m_Document{};
    uint64_t m_SourceVersion = 0;
    friend bool ReadLayoutSelection(const EditorClient &, LayoutSelectionData &);
};
struct LayoutRestoreData {
    ObjectRef group{}, camera{};
    std::vector<ObjectRef> selection;
    bool locked = false;
};
// A result is replaced only after every page has been read at one revision.
bool ReadLayoutScene(const EditorClient &, const char *section, LayoutSceneData &,
                     const std::vector<EditorValue> &arguments = {});
bool ReadLayoutSelection(const EditorClient &, LayoutSelectionData &);
} // namespace vdev
