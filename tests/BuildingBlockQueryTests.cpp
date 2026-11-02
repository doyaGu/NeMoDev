#include "core/document/Document.h"
#include "core/runtime/TextEncoding.h"
#include "editor/ImGuiDrawing.h"
#include "features/resources/BuildingBlockQueries.h"
#include "features/resources/BuildingBlockData.h"
#include "workspace/BuildingBlockDrag.h"
#include "workspace/EditorClient.h"
#include <stdexcept>
#include <thread>

namespace {
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void CatalogReplacement() {
    using namespace vdev;
    struct Source {
        uint64_t version = 1;
        size_t count = 131;
        unsigned fault = 0;
        EditorStatus Read(const EditorRequest &request, EditorSink sink) {
            const auto first = request.values[0].integer;
            const auto end = std::min<uint64_t>(count, first + 64);
            std::vector<EditorField> fields{{"revision", EDITOR_INTEGER_VALUE, {}, {}, version},
                {"total", EDITOR_INTEGER_VALUE, {}, {}, count + ((fault == 1 && first) ? 1 : 0)},
                {"names", EDITOR_TEXT_LIST}, {"categories", EDITOR_TEXT_LIST}, {"descriptions", EDITOR_TEXT_LIST},
                {"authors", EDITOR_TEXT_LIST}, {"compatible", EDITOR_TEXT_LIST},
                {"prototypes", EDITOR_INTEGER_LIST}, {"versions", EDITOR_INTEGER_LIST}, {"targetable", EDITOR_INTEGER_LIST}};
            for (uint64_t i = first; i < end; ++i) {
                fields[2].texts.push_back("Block " + std::to_string(version) + ":" + std::to_string(i));
                fields[3].texts.push_back(version == 1 ? "Old/Category" : "New/Category");
                for (size_t column = 4; column < 7; ++column) fields[column].texts.push_back("");
                fields[7].integers.push_back(fault == 2 && i == 65 ? version * 1000 + 1 : version * 1000 + i);
                fields[8].integers.push_back(1); fields[9].integers.push_back(0);
            }
            const auto borrowed = BorrowEditorFields(fields);
            const auto result = sink.write(sink.data, borrowed.data(), uint32_t(borrowed.size()));
            if (fault == 3 && end == count) ++version;
            return result;
        }
    } source;
    EditorHost host{};
    host.data = &source;
    host.scope = [](void *, EditorView) { return EditorScope{}; };
    host.queryRevision = [](void *data, EditorString, uint64_t *out) {
        *out = static_cast<Source *>(data)->version; return EDITOR_OK;
    };
    host.query = [](void *data, EditorString, const EditorRequest *request, EditorSink sink) {
        return static_cast<Source *>(data)->Read(*request, sink);
    };
    const EditorClient client(&host, 0);
    BuildingBlockData data;
    Check(data.Refresh(client) == EDITOR_OK && data.Entries().size() == 131, "initial copied catalog failed");
    const auto old = data.Entries().back();
    source.count = 2; ++source.version;
    Check(data.Refresh(client) == EDITOR_OK && data.Entries().size() == 2 && !data.Find(old.prototype) &&
          data.Categories().children.count("Old") == 0 && data.Categories().children.count("New") == 1 &&
          old.name == "Block 1:130", "replacement retained old catalog indices or borrowed results");
    const auto complete = data.Entries();
    const auto completeVersion = data.SourceVersion();
    for (unsigned fault : {1u, 2u, 3u}) {
        source.count = 131; ++source.version; source.fault = fault;
        const auto status = data.Refresh(client);
        Check(status == (fault == 2 ? EDITOR_INVALID : EDITOR_STALE) && data.SourceVersion() == completeVersion &&
              data.Entries().size() == complete.size() && data.Entries().front().prototype == complete.front().prototype,
              "inconsistent catalog replaced the last complete view data");
    }
    source.fault = 0; source.count = 0; ++source.version;
    Check(data.Refresh(client) == EDITOR_OK && data.Entries().empty() && data.Categories().children.empty(),
          "empty catalog retained old rows");
}
} // namespace
void TestBuildingBlockQueries(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    CatalogReplacement();
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error.c_str());
    BuildingBlockCatalog catalog;
    catalog.Refresh();
    bool available = true;
    EditorApplication app([&] { return &document; }, [&] { return available; });
    Check(RegisterBuildingBlockQueries(app, catalog, error), error.c_str());
    Check(app.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    app.Attach(document.Token(), document.TopologyRevision(), true, true);
    const auto undo = document.History().UndoCount();
    const auto revision = document.TopologyRevision();
    const bool dirty = document.Dirty();
    auto page = [&](uint64_t first) {
        const auto value = IntegerArgument("first", first);
        return app.Query("catalog.building-blocks", {app.Scope(), &value, 1});
    };
    BuildingBlockData data;
    Check(data.Refresh(EditorClient(app.Host(), 0)) == EDITOR_OK && data.Entries().size() == catalog.Entries().size() &&
              data.Revision() == catalog.Revision(),
          "shared creation-menu catalog differs from resource catalog");
    Check(data.Refresh(EditorClient(app.Host(), 0)) == EDITOR_OK && !data.Categories().children.empty(),
          "unchanged resource catalog could not be reused");
    size_t first = 0;
    const auto total = catalog.Entries().size();
    Check(total > 128, "Building Block fixtures missing");
    auto owned = page(0);
    const auto ownedName = EditorClient::Find(owned, "names")->texts[0];
    while (first < total) {
        const auto result = page(first);
        Check(result.status == EDITOR_OK && EditorClient::Integer(result, "total") == total &&
                  EditorClient::Integer(result, "revision") == catalog.Revision(),
              "catalog paging failed");
        const auto &names = EditorClient::Find(result, "names")->texts;
        const auto &paths = EditorClient::Find(result, "categories")->texts;
        const auto &descriptions = EditorClient::Find(result, "descriptions")->texts;
        const auto &authors = EditorClient::Find(result, "authors")->texts;
        const auto &compatible = EditorClient::Find(result, "compatible")->texts;
        const auto &prototypes = EditorClient::Find(result, "prototypes")->integers;
        const auto &versions = EditorClient::Find(result, "versions")->integers;
        const auto &targetable = EditorClient::Find(result, "targetable")->integers;
        const auto &classes = EditorClient::Find(result, "classes")->integers;
        Check(!names.empty() && names.size() <= 128 && paths.size() == names.size() &&
                  descriptions.size() == names.size() && authors.size() == names.size() &&
                  compatible.size() == names.size() && prototypes.size() == names.size() &&
                  versions.size() == names.size() && targetable.size() == names.size() &&
                  classes.size() == names.size(),
              "catalog columns differ");
        for (size_t i = 0; i < names.size(); ++i) {
            const auto &entry = catalog.Entries()[first + i];
            Check(names[i] == AnsiToUtf8(entry.name) && paths[i] == AnsiToUtf8(entry.category) &&
                      descriptions[i] == AnsiToUtf8(entry.description) && authors[i] == AnsiToUtf8(entry.author) &&
                      compatible[i] == AnsiToUtf8(entry.compatibleClassName) && versions[i] == entry.version &&
                      prototypes[i] == ((uint64_t(entry.guid.d1) << 32) | entry.guid.d2) &&
                      targetable[i] == uint64_t(entry.targetable) && classes[i] == uint64_t(entry.compatibleClass),
                  "projection differs from native Building Block catalog");
        }
        first += names.size();
    }
    Check(page(total).status == EDITOR_OK && page(total + 1).status == EDITOR_INVALID &&
              page(UINT64_MAX).status == EDITOR_INVALID,
          "invalid catalog offset accepted");
    available = false;
    Check(page(0).status == EDITOR_OK, "published catalog blocked by native work");
    uint64_t version = 123;
    EditorStatus status = EDITOR_OK;
    std::thread other([&] { status = app.QueryRevision("catalog.building-blocks", version); });
    other.join();
    Check(status == EDITOR_REFUSED && version == 123, "catalog wrong-thread access changed output");
    available = true;
    const auto before = catalog.Revision();
    catalog.Refresh();
    Check(catalog.Revision() != before && EditorClient::Integer(page(0), "revision") == catalog.Revision(),
          "catalog refresh kept stale query data");
    Check(EditorClient::Find(owned, "names")->texts[0] == ownedName, "owned catalog result changed after refresh");
    Check(document.History().UndoCount() == undo && document.TopologyRevision() == revision &&
              document.Dirty() == dirty,
          "catalog query changed document/history");

    const EditorBuildingBlock a{1, 2}, b{3, 4};
    const auto bytes = WriteBuildingBlockDrag(b, {a, b});
    EditorBuildingBlock source{7, 8};
    std::vector<EditorBuildingBlock> blocks{{9, 10}};
    Check(ReadBuildingBlockDrag(bytes.data(), bytes.size(), source, blocks) && source.first == 3 &&
              blocks.size() == 2 && blocks[0].first == 1 && blocks[1].first == 3,
          "drag lost source or insertion order");
    for (size_t size : {size_t(0), sizeof(EditorBuildingBlock), bytes.size() - 1, bytes.size() + 1})
        Check(!ReadBuildingBlockDrag(bytes.data(), size, source, blocks) && source.first == 3 && blocks.size() == 2,
              "malformed drag changed previous result");
    Check(!ReadBuildingBlockDrag(nullptr, bytes.size(), source, blocks), "null drag accepted");
    const auto badSource = WriteBuildingBlockDrag({17, 19}, {a, b});
    Check(!ReadBuildingBlockDrag(badSource.data(), badSource.size(), source, blocks),
          "drag source absent from list accepted");
    Check(WriteBuildingBlockDrag(a, {}).empty(), "empty drag accepted");
}
