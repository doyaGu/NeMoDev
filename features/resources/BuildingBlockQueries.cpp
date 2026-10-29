#include "features/resources/BuildingBlockQueries.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <iterator>

namespace vdev {
namespace {
uint64_t Version(void *data, const EditorHost *) {
    return static_cast<const BuildingBlockCatalog *>(data)->Revision();
}
EditorStatus Read(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
    const auto &catalog = *static_cast<const BuildingBlockCatalog *>(data);
    const auto &entries = catalog.Entries();
    const uint64_t first = request->values[0].integer;
    if (first > entries.size())
        return EDITOR_INVALID;
    const size_t end = std::min(entries.size(), size_t(first) + 128);
    std::vector<std::string> names, categories, descriptions, authors, compatible;
    std::vector<uint64_t> prototypes, versions, targetable, classes;
    for (size_t i = size_t(first); i < end; ++i) {
        const auto &entry = entries[i];
        names.push_back(AnsiToUtf8(entry.name));
        categories.push_back(AnsiToUtf8(entry.category));
        descriptions.push_back(AnsiToUtf8(entry.description));
        authors.push_back(AnsiToUtf8(entry.author));
        compatible.push_back(AnsiToUtf8(entry.compatibleClassName));
        prototypes.push_back((uint64_t(entry.guid.d1) << 32) | entry.guid.d2);
        versions.push_back(entry.version);
        targetable.push_back(entry.targetable);
        classes.push_back(entry.compatibleClass);
    }
    std::vector<EditorString> n, c, d, a, compatibleNames;
    auto strings = [](const auto &source, auto &result) {
        for (const auto &text : source)
            result.push_back(EditorText(text));
        return EditorStrings{result.data(), uint32_t(result.size())};
    };
    const EditorValue values[] = {
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE, {}, {}, catalog.Revision()},
        {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE, {}, {}, entries.size()},
        {EDITOR_TEXT("names"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, strings(names, n)},
        {EDITOR_TEXT("categories"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, strings(categories, c)},
        {EDITOR_TEXT("descriptions"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, strings(descriptions, d)},
        {EDITOR_TEXT("authors"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, strings(authors, a)},
        {EDITOR_TEXT("compatible"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, strings(compatible, compatibleNames)},
        {EDITOR_TEXT("prototypes"),
         EDITOR_INTEGER_LIST,
         {},
         {},
         0,
         {},
         {prototypes.data(), uint32_t(prototypes.size())}},
        {EDITOR_TEXT("versions"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {versions.data(), uint32_t(versions.size())}},
        {EDITOR_TEXT("targetable"),
         EDITOR_INTEGER_LIST,
         {},
         {},
         0,
         {},
         {targetable.data(), uint32_t(targetable.size())}},
        {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {classes.data(), uint32_t(classes.size())}}};
    return sink.write ? sink.write(sink.data, values, uint32_t(std::size(values))) : EDITOR_INVALID;
}
} // namespace
bool RegisterBuildingBlockQueries(EditorApplication &editor, const BuildingBlockCatalog &catalog, std::string &error) {
    const EditorParameter first{EDITOR_TEXT("first"), EDITOR_INTEGER_VALUE, 1};
    const EditorParameter fields[] = {
        {EDITOR_TEXT("revision"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("total"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("names"), EDITOR_TEXT_LIST},        {EDITOR_TEXT("categories"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("descriptions"), EDITOR_TEXT_LIST}, {EDITOR_TEXT("authors"), EDITOR_TEXT_LIST},
        {EDITOR_TEXT("compatible"), EDITOR_TEXT_LIST},   {EDITOR_TEXT("prototypes"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("versions"), EDITOR_INTEGER_LIST},  {EDITOR_TEXT("targetable"), EDITOR_INTEGER_LIST},
        {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST}};
    const EditorQueryDesc query{sizeof(EditorQueryDesc),
                                EDITOR_TEXT("catalog.building-blocks"),
                                EDITOR_TEXT("builtin.building-blocks"),
                                &first,
                                1,
                                Read,
                                const_cast<BuildingBlockCatalog *>(&catalog),
                                EDITOR_QUERY_VERSIONED,
                                EDITOR_WORKSPACE,
                                fields,
                                uint32_t(std::size(fields)),
                                Version};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("resources.catalog");
    module.queries = &query;
    module.queryCount = 1;
    return editor.Register(module, error);
}
} // namespace vdev
