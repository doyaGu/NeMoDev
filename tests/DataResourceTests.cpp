#include "core/document/BehaviorDescription.h"
#include "core/document/Document.h"
#include "core/document/ToolCommands.h"
#include "core/level/LevelCommands.h"
#include "editor/ImGuiDrawing.h"
#include "features/resources/DataResourceCommands.h"
#include "features/resources/DataResourceViews.h"
#include "workspace/EditorClient.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/ResourceDrag.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::string Bytes(const std::string &path) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
struct Remove {
    std::filesystem::path path;
    ~Remove() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
} // namespace
void TestDataResources(vdev::CkRuntimeHost &runtime) {
    using namespace vdev;
    namespace fs = std::filesystem;
    Document document(runtime);
    std::string error;
    Check(document.New(&error), error.c_str());
    const auto made = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(made.ok && RenameLevelObject(document, document.Reference(made.created), "Resource tests").ok,
          "prepare resource test history");
    const auto token = document.Token();
    const auto undo = document.History().UndoCount();
    const auto topology = document.TopologyRevision();
    const auto dirty = document.Dirty();
    bool native = true;
    EditorApplication editor([&] { return &document; }, [&] { return native; });
    EditorWorkspace workspace(editor);
    DataResourceCommands resources(editor, runtime, [&] { return native; });
    Check(workspace.Register(error) && resources.Register(error) && RegisterDataResourceViews(editor, error),
          error.c_str());
    Check(editor.Activate(EDITOR_IMGUI_VERSION, error), error.c_str());
    editor.Attach(token, topology, true, true);
    const auto selectedObject = document.Reference(made.created);
    editor.SetSelection(selectedObject);
    EditorClient client(editor.Host(), 0);
    std::vector<EditorResponse> notices;
    editor.report = [&](const EditorResponse &result) { notices.push_back(result); };
    const auto root = fs::temp_directory_path() / "nemodev-resource-query-tests";
    fs::remove_all(root);
    fs::create_directories(root);
    Remove cleanup{root};
    const auto library = (root / fs::u8path("素材.rsc")).u8string();
    Check(CreateDataResource(library, &error), error.c_str());
    const auto folder = (root / fs::u8path("素材/Behavior Graphs")).u8string();
    for (int i = 0; i < 141; ++i) {
        std::ofstream(fs::u8path(folder) / ("Graph" + std::to_string(i) + ".nms")) << "fixture";
    }
    const auto behavior = (fs::u8path(folder) / "Graph0.nms").u8string();
    BehaviorDescription description;
    description.name = "Original";
    description.author = "Author";
    description.version = 1.5f;
    description.description = "First line\nSecond line";
    description.cid = CKCID_3DENTITY;
    Check(WriteBehaviorDescription(behavior, description), "write description fixture");
    int changes = 0;
    resources.changed = [&] { ++changes; };
    const auto opened = client.Command("resource.open-library", {TextArgument("path", library)});
    const auto first = EditorClient::Integer(opened, "view");
    Check(opened.status == EDITOR_OK && first && workspace.ViewCount() == 1,
          "open library did not create a registered view");
    const auto again = client.Command("resource.open-library", {TextArgument("path", library)});
    Check(again.status == EDITOR_OK && EditorClient::Integer(again, "view") == first && workspace.ViewCount() == 1,
          "opening an existing library duplicated its view");
    resources.Poll();
    Check(resources.OpenLibraries() == std::vector<std::string>{library} && changes == 1,
          "open library list not published");
    resources.Poll();
    Check(changes == 1, "stable resource list republished");
    auto page = [&](uint64_t first) {
        return client.Query("resource.files", {TextArgument("path", library), TextArgument("folder", folder),
                                               IntegerArgument("first", first)});
    };
    size_t count = 0;
    while (count < 141) {
        const auto result = page(count);
        const auto *paths = EditorClient::Find(result, "paths");
        Check(result.status == EDITOR_OK && paths && !paths->texts.empty() && paths->texts.size() <= 64 &&
                  EditorClient::Integer(result, "total") == 141,
              "resource file paging failed");
        count += paths->texts.size();
    }
    Check(page(142).status == EDITOR_INVALID && page(UINT64_MAX).status == EDITOR_INVALID,
          "out-of-range resource page accepted");
    const auto copy = page(0);
    Check(EditorClient::Find(copy, "authors")->texts[0] == "Author" &&
              EditorClient::Find(copy, "descriptions")->texts[0] == description.description,
          "description projection differs");
    auto navigate = [&](EditorView view, const std::string &folder, const std::vector<EditorString> &files = {}) {
        return client
            .Command(
                "resource.browse",
                {IntegerArgument("view", view),
                 TextArgument("folder", folder),
                 {EDITOR_TEXT("files"), EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, {files.data(), uint32_t(files.size())}}})
            .status;
    };
    const auto selected = EditorText(behavior);
    Check(navigate(first, "Behavior Graphs", {selected}) == EDITOR_OK, "resource navigation failed");
    const auto before = client.Inspect(first);
    Check(navigate(first, "Missing folder") == EDITOR_INVALID &&
              EditorClient::Text(client.Inspect(first), "folder") == folder &&
              EditorClient::Find(client.Inspect(first), "selection")->texts ==
                  EditorClient::Find(before, "selection")->texts,
          "failed navigation changed existing view");
    const auto second = workspace.Open("resource.data");
    Check(client.Command("resource.open-library", {TextArgument("path", library), IntegerArgument("view", second)})
                      .status == EDITOR_OK &&
              EditorClient::Text(client.Inspect(second), "folder") != folder,
          "second resource view inherited navigation");
    native = false;
    Check(page(0).status == EDITOR_OK && client.Inspect(first).status == EDITOR_OK,
          "published resource browsing blocked by native work");
    Check(client.Command("resource.open-library", {TextArgument("path", library)}).status == EDITOR_REFUSED,
          "resource path mutation overlapped native work");
    native = true;
    EditorStatus threadStatus = EDITOR_OK;
    std::thread other([&] { threadStatus = page(0).status; });
    other.join();
    Check(threadStatus == EDITOR_REFUSED, "resource query accepted wrong thread");
    Check(client.Command("resource.open-library", {TextArgument("path", (root / "missing.rsc").u8string())}).status ==
                  EDITOR_ERROR &&
              workspace.ViewCount() == 2,
          "failed library open removed an existing view");
    auto information = [&] {
        return client.Command("resource.behavior-information", {TextArgument("path", behavior)});
    };
    auto respond = [&](const char *choice, const std::string &version = "2.5") {
        const auto input = EditorClient::Integer(client.Query("resource.input"), "input");
        return client.Command("resource.respond", {IntegerArgument("input", input), TextArgument("choice", choice),
                                                   TextArgument("author", "Edited"), TextArgument("version", version),
                                                   TextArgument("description", "Updated\nDescription")});
    };
    const auto original = Bytes(BehaviorDescriptionPath(behavior));
    Check(information().status == EDITOR_QUEUED && respond("cancel").status == EDITOR_OK &&
              Bytes(BehaviorDescriptionPath(behavior)) == original,
          "cancel wrote resource description");
    const auto edit = information();
    Check(edit.status == EDITOR_QUEUED && respond("accept", "nan").status == EDITOR_INVALID &&
              Bytes(BehaviorDescriptionPath(behavior)) == original,
          "invalid resource version changed file");
    Check(respond("accept").status == EDITOR_OK && client.Completion(edit.operation).status == EDITOR_OK &&
              ReadBehaviorDescription(behavior, description) && description.author == "Edited" &&
              description.version == 2.5f && EditorClient::Find(page(0), "authors")->texts[0] == "Edited" &&
              EditorClient::Find(copy, "authors")->texts[0] == "Author",
          "resource description did not refresh published data");
    const auto deferred = information();
    const auto previous = Bytes(BehaviorDescriptionPath(behavior));
    native = false;
    Check(respond("accept", "3.5").status == EDITOR_OK &&
              client.Completion(deferred.operation).status == EDITOR_QUEUED &&
              Bytes(BehaviorDescriptionPath(behavior)) == previous,
          "resource input wrote during native work");
    resources.Poll();
    native = true;
    resources.Poll();
    Check(client.Completion(deferred.operation).status == EDITOR_OK && ReadBehaviorDescription(behavior, description) &&
              description.version == 3.5f,
          "deferred resource input was lost");
    const auto savedDescription = BehaviorDescriptionPath(behavior);
    fs::rename(fs::u8path(savedDescription), fs::u8path(savedDescription + ".keep"));
    fs::create_directory(fs::u8path(savedDescription));
    const auto failedWrite = information();
    Check(respond("accept").status == EDITOR_OK && client.Completion(failedWrite.operation).status == EDITOR_ERROR &&
              fs::is_directory(fs::u8path(savedDescription)),
          "failed resource description publication changed destination");
    Check(notices.size() == 1 && notices.back().status == EDITOR_ERROR &&
              notices.back().message.find(behavior) != std::string::npos,
          "resource publication failure was not reported with its output path");
    fs::remove(fs::u8path(savedDescription));
    fs::rename(fs::u8path(savedDescription + ".keep"), fs::u8path(savedDescription));
    const auto owner = workspace.Open("resource.data");
    EditorClient fromView(editor.Host(), owner);
    const auto abandoned = fromView.Command("resource.open-library");
    Check(abandoned.status == EDITOR_QUEUED && workspace.Close(owner), "prepare resource input owner closure");
    resources.Poll();
    Check(client.Completion(abandoned.operation).status == EDITOR_CANCELLED &&
              client.Query("resource.input").status == EDITOR_NO_CHANGE,
          "closed view retained resource input");
    const auto originalRoot = root / fs::u8path("素材");
    fs::rename(originalRoot, root / "temporarily-missing");
    Check(client.Command("resource.refresh-library", {TextArgument("path", library)}).status == EDITOR_ERROR &&
              page(0).status == EDITOR_OK,
          "failed resource refresh discarded published directory data");
    fs::rename(root / "temporarily-missing", originalRoot);
    std::error_code linkError;
    fs::create_directory_symlink(originalRoot, originalRoot / "cycle", linkError);
    if (!linkError) {
        Check(client.Command("resource.refresh-library", {TextArgument("path", library)}).status == EDITOR_ERROR &&
                  page(0).status == EDITOR_OK,
              "recursive resource directory replaced published data");
        fs::remove(originalRoot / "cycle");
    }
    const auto pending = client.Command("resource.open-library");
    Check(pending.status == EDITOR_QUEUED && client.Command("resource.respond", {IntegerArgument("input", UINT64_MAX),
                                                                                 TextArgument("choice", "cancel")})
                                                     .status == EDITOR_STALE,
          "old resource response accepted");
    Check(respond("cancel").status == EDITOR_OK && client.Completion(pending.operation).status == EDITOR_CANCELLED,
          "resource chooser cancel did not complete");
    const auto create =
        client.Command("resource.create-library", {TextArgument("path", (root / "Created").u8string())});
    Check(create.status == EDITOR_OK && fs::exists(root / "Created.rsc"), "resource creation command failed");
    resources.Poll();
    Check(resources.OpenLibraries().size() == 2, "second library was not remembered");
    Check(workspace.Close(first), "close first resource view");
    resources.Poll();
    Check(page(0).status == EDITOR_OK, "closing one resource view discarded another view's catalog");
    Check(workspace.Close(second), "close second resource view");
    resources.Poll();
    Check(page(0).status == EDITOR_STALE && resources.OpenLibraries().size() == 1,
          "closed resource retained its catalog");
    const auto written = fs::file_size(root / "Created.rsc");
    Check(document.Token() == token && document.History().UndoCount() == undo &&
              document.TopologyRevision() == topology && document.Dirty() == dirty &&
              editor.Selection() == selectedObject && written > 0,
          "resource browsing changed document/history");
    workspace.CloseAll();
    resources.Stop();
    Check(page(0).status == EDITOR_STALE &&
              client.Command("resource.open-library", {TextArgument("path", library)}).status == EDITOR_REFUSED,
          "stopped resource owner served cached data or accepted new work");

    const std::vector<ResourceFile> files{{std::string(3000, 'x') + "/路径 含空格/First.nms", 8},
                                         {std::string(3000, 'y') + "/Second image.bmp", 31}};
    const auto bytes = WriteResourceDrag(1, files);
    uint32_t source = 9;
    std::vector<ResourceFile> decoded{{"Keep", 0}};
    Check(ReadResourceDrag(bytes.data(), bytes.size(), source, decoded) && source == 1 && decoded.size() == 2 &&
              decoded[0].path == files[0].path,
          "resource drag truncated paths or source");
    for (size_t size : {size_t(0), bytes.size() - 1, bytes.size() + 1})
        Check(!ReadResourceDrag(bytes.data(), size, source, decoded) && source == 1 && decoded.size() == 2,
              "malformed resource drag changed previous output");
    Check(!ReadResourceDrag(nullptr, bytes.size(), source, decoded) && WriteResourceDrag(2, files).empty() &&
              WriteResourceDrag(0, {}).empty(),
          "invalid resource drag accepted");
    std::vector<unsigned char> unaligned(bytes.size() + 1);
    std::memcpy(unaligned.data() + 1, bytes.data(), bytes.size());
    Check(ReadResourceDrag(unaligned.data() + 1, bytes.size(), source, decoded) && source == 1 &&
              decoded[1].path == files[1].path && decoded[1].cid == files[1].cid,
          "long resource paths required aligned drag storage or lost the grabbed row");
    const auto limit = size_t(1048576) - sizeof(EditorResourceDragHeader) - sizeof(EditorResourceFileHeader);
    const auto largest = WriteResourceDrag(0, {{std::string(limit, 'z'), 31}});
    Check(largest.size() == 1048576 && ReadResourceDrag(largest.data(), largest.size(), source, decoded) &&
              source == 0 && decoded.size() == 1 && decoded.front().path.size() == limit,
          "resource drag rejected its documented size limit");
    Check(WriteResourceDrag(0, {{std::string(limit + 1, 'z'), 31}}).empty(),
          "oversized resource drag was not rejected");
}
