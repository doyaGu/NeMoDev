#include "features/scene/SceneViewData.h"
#include <stdexcept>
namespace vdev {
EditorStatus QuerySceneTree(const EditorClient &client, const SceneTreeOptions &options, bool hierarchy,
                            SceneTree &output) {
    SceneTree tree;
    tree.document = client.Scope().document;
    uint64_t first = 0, total = 0, version = 0;
    do {
        const auto page = client.Query(
            "scene.tree",
            {IntegerArgument("first", first), IntegerArgument("hierarchy", hierarchy),
             IntegerArgument("priority", options.priority), IntegerArgument("reversed", options.reversed),
             ObjectArgument("context", options.context),
             IntegerArgument("resources", options.meshes | (options.materials << 1) | (options.textures << 2))});
        if (page.status != EDITOR_OK) {
            return page.status;
        }
        const auto current = EditorClient::Integer(page, "revision");
        if (first && current != version) {
            return EDITOR_INVALID;
        }
        version = current;
        total = EditorClient::Integer(page, "total");
        const auto names = SceneTexts(page, "names"), owners = SceneTexts(page, "owners");
        const auto keys = SceneIntegers(page, "keys"), parents = SceneIntegers(page, "parents"),
                   kinds = SceneIntegers(page, "kinds"), types = SceneIntegers(page, "types"),
                   cells = SceneIntegers(page, "cells"), classes = SceneIntegers(page, "classes"),
                   priorities = SceneIntegers(page, "priorities"), zorders = SceneIntegers(page, "render-priorities"),
                   icons = SceneIntegers(page, "icons"), worksets = SceneIntegers(page, "worksets"),
                   roots = SceneIntegers(page, "roots"), saves = SceneIntegers(page, "save-options"),
                   expanded = SceneIntegers(page, "expanded");
        const auto objects = SceneObjects(page, "objects"), scenes = SceneObjects(page, "scenes"),
                   sections = SceneObjects(page, "sections"), groups = SceneObjects(page, "groups"),
                   scripts = SceneObjects(page, "scripts");
        const size_t size = names.size();
        for (auto n :
             {owners.size(), keys.size(), parents.size(), kinds.size(), types.size(), cells.size(), classes.size(),
              priorities.size(), zorders.size(), icons.size(), worksets.size(), roots.size(), saves.size(),
              expanded.size(), objects.size(), scenes.size(), sections.size(), groups.size(), scripts.size()}) {
            if (n != size) {
                return EDITOR_INVALID;
            }
        }
        if ((!size && first < total) || total > 1000000 || first + size > total) {
            return EDITOR_INVALID;
        }
        for (size_t i = 0; i < size; ++i) {
            if (parents[i] > first + i) {
                return EDITOR_INVALID;
            }
            SceneRow row;
            row.key = keys[i];
            row.parent = uint32_t(parents[i]);
            row.depth = row.parent ? tree.rows[row.parent - 1].depth + 1 : 0;
            row.object = objects[i];
            row.scene = scenes[i];
            row.section = sections[i];
            row.group = groups[i];
            row.script = scripts[i];
            row.kind = uint32_t(kinds[i]);
            row.types = uint32_t(types[i]);
            row.cells = uint32_t(cells[i]);
            row.cid = uint32_t(classes[i]);
            row.priority = int(int64_t(priorities[i]));
            row.renderPriority = int(int64_t(zorders[i]));
            row.icon = int(icons[i]);
            row.workset = int(worksets[i]);
            row.root = int(int64_t(roots[i]));
            row.saveOption = int(int64_t(saves[i]));
            row.expanded = expanded[i] != 0;
            row.name = names[i];
            row.owner = owners[i];
            tree.rows.push_back(std::move(row));
        }
        tree.level = SceneObject(page, "level");
        tree.levelScene = SceneObject(page, "level-scene");
        tree.currentScene = SceneObject(page, "current-scene");
        first += size;
    } while (first < total);
    output = std::move(tree);
    return EDITOR_OK;
}
SceneAttributeCatalog QuerySceneAttributes(const EditorClient &client, const std::vector<ObjectRef> &targets) {
    const auto result = client.Query("attribute.catalog", {ObjectsArgument("objects", targets)});
    SceneAttributeCatalog catalog;
    if (result.status != EDITOR_OK) {
        return catalog;
    }
    catalog.version = EditorClient::Integer(result, "catalog");
    catalog.available = EditorClient::Integer(result, "available") != 0;
    const auto names = SceneTexts(result, "names"), parameters = SceneTexts(result, "parameters"),
               compatible = SceneTexts(result, "compatible"), categories = SceneTexts(result, "categories");
    const auto ids = SceneIntegers(result, "types"), flags = SceneIntegers(result, "flags"),
               parents = SceneIntegers(result, "parents"), categoryFlags = SceneIntegers(result, "category-flags");
    if (names.size() != ids.size() || parameters.size() != ids.size() || compatible.size() != ids.size() ||
        flags.size() != ids.size() || parents.size() != ids.size() || categories.size() != categoryFlags.size()) {
        return {};
    }
    for (size_t i = 0; i < categories.size(); ++i) {
        catalog.categories.push_back({categories[i], categoryFlags[i], {}});
    }
    for (size_t i = 0; i < ids.size(); ++i) {
        catalog.types.push_back({ids[i], flags[i], parents[i], names[i], parameters[i], compatible[i]});
        if (parents[i] && parents[i] <= catalog.categories.size()) {
            catalog.categories[parents[i] - 1].types.push_back(ids[i]);
        }
    }
    return catalog;
}
std::vector<SceneAttributeRow> QueryAttributeObjects(const EditorClient &client, const SceneAttributeCatalog &catalog,
                                                     const std::set<uint64_t> &types,
                                                     const std::vector<ObjectRef> &targets) {
    const std::vector<uint64_t> selected(types.begin(), types.end());
    std::vector<SceneAttributeRow> rows;
    uint64_t first = 0, total = 0;
    do {
        const auto page = client.Query(
            "attribute.objects",
            {IntegerArgument("first", first),
             ObjectsArgument("objects", targets),
             IntegerArgument("catalog", catalog.version),
             {EDITOR_TEXT("types"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {selected.data(), uint32_t(selected.size())}}});
        if (page.status != EDITOR_OK) {
            return {};
        }
        const auto objects = SceneObjects(page, "objects"), parameters = SceneObjects(page, "parameters");
        const auto ids = SceneIntegers(page, "types");
        const auto names = SceneTexts(page, "names"), values = SceneTexts(page, "values"),
                   parameterNames = SceneTexts(page, "parameter-names");
        const size_t size = objects.size();
        total = EditorClient::Integer(page, "total");
        if (parameters.size() != size || ids.size() != size || names.size() != size || values.size() != size ||
            parameterNames.size() != size || (!size && first < total) || total > 1000000 || first + size > total) {
            return {};
        }
        for (size_t i = 0; i < size; ++i) {
            rows.push_back({objects[i], parameters[i], ids[i], names[i], values[i], parameterNames[i]});
        }
        first += size;
    } while (first < total);
    return rows;
}
bool QueryObjectAttributes(const EditorClient &client, ObjectRef target, ObjectAttributes &output) {
    ObjectAttributes next;
    next.target = target;
    uint64_t first = 0, count = 0;
    std::set<uint64_t> types;
    do {
        const auto page =
            client.Query("object.attributes", {ObjectArgument("target", target), IntegerArgument("first", first)});
        const auto *echo = EditorClient::Find(page, "target"), *rows = EditorClient::Find(page, "rows");
        const auto *catalog = EditorClient::Find(page, "catalog"),
                   *revision = EditorClient::Find(page, "document-revision");
        const auto *behavioral = EditorClient::Find(page, "behavioral"),
                   *available = EditorClient::Find(page, "available");
        const auto *total = EditorClient::Find(page, "total"), *end = EditorClient::Find(page, "next");
        if (page.status != EDITOR_OK || !target || target.document != client.Scope().document || !echo ||
            echo->type != EDITOR_OBJECT || echo->object != target || !rows || rows->type != EDITOR_RECORD || !catalog ||
            catalog->type != EDITOR_INTEGER_VALUE || !revision || revision->type != EDITOR_INTEGER_VALUE ||
            !behavioral || behavioral->type != EDITOR_INTEGER_VALUE || behavioral->integer > 1 || !available ||
            available->type != EDITOR_INTEGER_VALUE || available->integer > 1 || !total ||
            total->type != EDITOR_INTEGER_VALUE || total->integer > 1000000 || !end ||
            end->type != EDITOR_INTEGER_VALUE || end->integer > total->integer || end->integer < first ||
            rows->fields.size() != end->integer - first ||
            ((!behavioral->integer || !available->integer) && total->integer) ||
            (end->integer == first && end->integer != total->integer))
            return false;
        if (first && (count != total->integer || next.catalog != catalog->integer ||
                      next.documentRevision != revision->integer || next.behavioral != bool(behavioral->integer) ||
                      next.available != bool(available->integer)))
            return false;
        next.catalog = catalog->integer;
        next.documentRevision = revision->integer;
        count = total->integer;
        next.behavioral = behavioral->integer != 0;
        next.available = available->integer != 0;
        for (size_t i = 0; i < rows->fields.size(); ++i) {
            const auto &row = rows->fields[i];
            if (row.type != EDITOR_RECORD || row.fields.size() != 6 || row.key != std::to_string(first + i))
                return false;
            const auto field = [&](const char *key, EditorValueType type) -> const EditorField * {
                for (const auto &f : row.fields)
                    if (f.key == key && f.type == type)
                        return &f;
                return nullptr;
            };
            const auto *id = field("type", EDITOR_INTEGER_VALUE), *parameter = field("parameter", EDITOR_OBJECT),
                       *name = field("name", EDITOR_TEXT_VALUE), *category = field("category", EDITOR_TEXT_VALUE),
                       *value = field("value", EDITOR_TEXT_VALUE),
                       *parameterName = field("parameter-name", EDITOR_TEXT_VALUE);
            if (!id || id->integer > INT32_MAX || !types.insert(id->integer).second || !parameter ||
                (parameter->object && parameter->object.document != target.document) || !name || !category || !value ||
                !parameterName)
                return false;
            if (!parameter->object &&
                (parameter->object.document.value || parameter->object.object || parameter->object.lifetime))
                return false;
            next.rows.push_back(
                {id->integer, parameter->object, name->text, category->text, value->text, parameterName->text});
        }
        first = end->integer;
        if (first == total->integer)
            break;
    } while (true);
    output = std::move(next);
    return true;
}
bool QueryAttributeClipboard(const EditorClient &client, AttributeClipboard &output) {
    const auto result = client.Query("attribute.clipboard");
    const auto *revision = EditorClient::Find(result, "revision"), *names = EditorClient::Find(result, "names");
    if (result.status != EDITOR_OK || !revision || revision->type != EDITOR_INTEGER_VALUE || !names ||
        names->type != EDITOR_TEXT_LIST)
        return false;
    output = {revision->integer, names->texts};
    return true;
}
void SceneSelection::Pull(const EditorClient &client) {
    const auto state = client.Query("selection.objects");
    if (state.status != EDITOR_OK) {
        return;
    }
    const auto revision = EditorClient::Integer(state, "revision");
    locked = EditorClient::Integer(state, "locked") != 0;
    if (m_Operation) {
        const auto operation = client.Completion(m_Operation);
        if (operation.status != EDITOR_QUEUED) {
            if ((operation.status == EDITOR_OK || operation.status == EDITOR_NO_CHANGE) &&
                revision == EditorClient::Integer(operation, "revision")) {
                m_Seen = revision;
            }
            m_Operation = 0;
        }
    }
    if (revision != m_Seen) {
        const auto refs = SceneObjects(state, "objects");
        objects = {refs.begin(), refs.end()};
        m_Seen = revision;
    }
    m_Last = objects;
}
void SceneSelection::Push(const EditorClient &client) {
    if (locked || objects == m_Last) {
        return;
    }
    const std::vector<ObjectRef> refs(objects.begin(), objects.end());
    const auto result =
        client.Command("selection.replace", {ObjectsArgument("objects", refs), IntegerArgument("render-only", 1)});
    if (result.status == EDITOR_QUEUED) {
        m_Operation = result.operation;
    } else if (result.status == EDITOR_OK || result.status == EDITOR_NO_CHANGE) {
        m_Seen = EditorClient::Integer(result, "revision");
    }
    m_Last = objects;
}
} // namespace vdev
