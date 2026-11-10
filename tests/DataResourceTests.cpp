#include "core/document/BehaviorDescription.h"
#include "core/document/Document.h"
#include "core/document/ToolCommands.h"
#include "core/level/LevelCommands.h"
#include "core/document/SaveFileOperations.h"
#include "core/runtime/TextEncoding.h"
#include "editor/ImGuiDrawing.h"
#include "features/resources/DataResourceCommands.h"
#include "features/resources/DataResourceViews.h"
#include "workspace/EditorClient.h"
#include "workspace/EditorWorkspace.h"
#include "workspace/ResourceDrag.h"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {
void Check(bool ok, const std::string &message) {
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
    Check(document.New(&error), error);
    const auto made = CreateLayoutObject(document, LayoutObject::Frame3D);
    Check(made.ok && RenameLevelObject(document, document.Reference(made.created), "Resource tests").ok,
          "prepare resource test history");
    const auto token = document.Token();
    const auto undo = document.History().UndoCount();
    const auto topology = document.TopologyRevision();
    const auto dirty = document.Dirty();
    bool native = true;
    RuntimeOperation operations({}, {}, [&] { return native; });
    EditorApplication editor([&] { return &document; }, [&] { return operations.Available(); });
    EditorWorkspace workspace(editor);
    DataResourceCommands resources(editor, operations, runtime);
    Check(workspace.Register(error) && resources.Register(error) && RegisterDataResourceViews(editor, error),
          error);
    Check(editor.Activate(EDITOR_IMGUI_VERSION, error), error);
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
    struct CreateFailure : SaveFileOperations {
        unsigned calls = 0, fail = 0;
        std::error_code Replace(const fs::path &from, const fs::path &to, bool overwrite) override {
            if (++calls == fail) return std::make_error_code(std::errc::io_error);
            return SaveFileOperations::Replace(from, to, overwrite);
        }
    } creation;
    const auto existing = (root / "retained.rsc").u8string();
    Check(CreateDataResource(existing, &error), error);
    const auto retained = root / "retained";
    { std::ofstream keep(retained / "Keep.txt"); keep << "Existing resource data"; }
    std::map<fs::path, std::string> previousResourceFiles;
    previousResourceFiles[fs::u8path(existing)] = Bytes(existing);
    for (const auto &entry : fs::recursive_directory_iterator(retained))
        if (entry.is_regular_file()) previousResourceFiles[entry.path()] = Bytes(entry.path().u8string());
    for (unsigned failed = 1; failed <= 10; ++failed) {
        creation.calls = 0; creation.fail = failed;
        Check(!CreateDataResource(existing, &error, &creation) && !error.empty(), "resource creation ignored a publication failure");
        for (const auto &file : previousResourceFiles)
            Check(Bytes(file.first.u8string()) == file.second, "failed resource creation changed an existing file");
        const auto fresh = (root / "fresh.rsc").u8string();
        creation.calls = 0;
        Check(!CreateDataResource(fresh, &error, &creation) && !fs::exists(fs::u8path(fresh)) && !fs::exists(root / "fresh"),
              "failed resource creation retained newly created files or folders");
    }
    for (const auto &entry : fs::recursive_directory_iterator(root))
        Check(entry.path().filename().u8string().find(".nemodev-save-") != 0, "resource creation leaked a temporary file");
    const auto unicodeLibrary = (root / fs::u8path(u8"素材.rsc")).u8string();
    Check(CreateDataResource(unicodeLibrary, &error), error);
    DataResource unicode;
    Check(ReadDataResource(unicodeLibrary, unicode, &error) && unicode.root.name == u8"素材", error);
    bool nativeUnicode = true;
#ifdef _WIN32
    try { (void)Utf8ToAnsi(u8"素材"); }
    catch (const TextEncodingError &) { nativeUnicode = false; }
#endif
    auto *paths = document.Context()->GetPathManager();
    std::vector<int> pathCounts;
    for (int i = 0; i < paths->GetCategoryCount(); ++i) pathCounts.push_back(paths->GetPathCount(i));
    const auto unicodePaths = AddDataResourcePaths(document.Context(), unicode);
    Check(nativeUnicode ? unicodePaths > 0 : unicodePaths == 0, "native resource path encoding was not respected");
    for (int i = 0; i < int(pathCounts.size()); ++i) {
        if (!nativeUnicode) Check(paths->GetPathCount(i) == pathCounts[i], "unrepresentable paths changed the SDK search list");
        while (paths->GetPathCount(i) > pathCounts[i])
            Check(paths->RemovePath(i, paths->GetPathCount(i) - 1) == CK_OK, "restore native resource paths");
    }
    // Native SDK file operations keep the system encoding. The independent
    // UTF-8 scan above covers names outside that encoding without changing it.
    const std::string libraryStem = nativeUnicode ? u8"素材" : "resources";
    const auto library = (root / fs::u8path(libraryStem + ".rsc")).u8string();
    Check(CreateDataResource(library, &error), error);
    const auto folder = (root / fs::u8path(libraryStem) / "Behavior Graphs").u8string();
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
    const auto finish = [&](EditorResponse result) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (result.status == EDITOR_QUEUED && std::chrono::steady_clock::now() < deadline) {
            resources.Poll();
            const auto operation = result.operation;
            result = client.Completion(operation);
            if (result.status == EDITOR_QUEUED) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(result.status != EDITOR_QUEUED, "resource scan did not complete");
        return result;
    };
    if (!nativeUnicode) {
        const auto shown = finish(client.Command("resource.open-library", {TextArgument("path", unicodeLibrary)}));
        const auto view = EditorClient::Integer(shown, "view");
        Check(shown.status == EDITOR_OK && view && workspace.ViewCount() == 1,
              "UTF-8 library browsing depended on the SDK character set: " + shown.message);
        Check(EditorClient::Text(client.Inspect(view), "library") == unicodeLibrary,
              "UTF-8 library navigation changed its path");
        for (int i = 0; i < int(pathCounts.size()); ++i)
            Check(paths->GetPathCount(i) == pathCounts[i], "browsing registered an unrepresentable SDK path");
        Check(workspace.Close(view), "close UTF-8 resource view");
        resources.Poll();
        changes = 0;
        notices.clear();
    }
    const auto scanning = client.Command("resource.open-library", {TextArgument("path", library)});
    Check(scanning.status == EDITOR_QUEUED && operations.Available() && workspace.ViewCount() == 0,
          "directory scanning blocked the caller or native rendering");
    native = false;
    resources.Poll();
    Check(client.Completion(scanning.operation).status == EDITOR_QUEUED &&
          client.Completion(scanning.operation).operation == scanning.operation && workspace.ViewCount() == 0,
          "scan published native paths while the runtime was unavailable");
    native = true;
    const auto opened = finish(scanning);
    const auto first = EditorClient::Integer(opened, "view");
    if (opened.status != EDITOR_OK || !first || workspace.ViewCount() != 1)
        throw std::runtime_error("open library did not create a registered view: status=" + std::to_string(opened.status) +
            " view=" + std::to_string(first) + " count=" + std::to_string(workspace.ViewCount()) + " " + opened.message);
    const auto refreshOne = client.Command("resource.refresh-library", {TextArgument("path", library)});
    const auto refreshTwo = client.Command("resource.refresh-library", {TextArgument("path", library)});
    Check(refreshOne.status == EDITOR_QUEUED && refreshTwo.status == EDITOR_QUEUED &&
          client.Command("resource.open-library").status == EDITOR_REFUSED,
          "resource scans did not queue independently of chooser input");
    Check(finish(refreshOne).status == EDITOR_OK && finish(refreshTwo).status == EDITOR_OK,
          "queued resource scans lost their completion");
    const uint64_t importClasses[] = {CKCID_TEXTURE, CKCID_SPRITE, CKCID_SPRITE3D, CKCID_MESH};
    for (uint64_t kind = 0; kind < 5; ++kind) {
        const auto resolved = client.Query("resource.import-classes", {
            {EDITOR_TEXT("classes"), EDITOR_INTEGER_LIST, {}, {}, 0, {}, {importClasses, 4}}, IntegerArgument("image-kind", kind)});
        const uint64_t targets[] = {0, CKCID_TEXTURE, CKCID_SPRITE, CKCID_SPRITE3D, CKCID_2DENTITY};
        const auto *classes = EditorClient::Find(resolved, "classes"), *images = EditorClient::Find(resolved, "images");
        Check(resolved.status == EDITOR_OK && classes && images && classes->integers.size() == 4 &&
              images->integers == std::vector<uint64_t>({1, 1, 1, 0}) && classes->integers[3] == CKCID_MESH,
              "resource image classification changed non-image inputs");
        for (size_t i = 0; i < 3; ++i) Check(classes->integers[i] == (kind ? targets[kind] : importClasses[i]),
                                           "resource image modifier mapping differs from native imports");
    }
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
    Check(finish(client.Command("resource.open-library", {TextArgument("path", (root / "missing.rsc").u8string())})).status ==
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
    const auto noticesBeforeFailure = notices.size();
    const auto failedWrite = information();
    Check(respond("accept").status == EDITOR_OK && client.Completion(failedWrite.operation).status == EDITOR_ERROR &&
              fs::is_directory(fs::u8path(savedDescription)),
          "failed resource description publication changed destination");
    Check(notices.size() == noticesBeforeFailure + 1 && notices.back().status == EDITOR_ERROR &&
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
    const auto cancelledPath = (root / "Cancelled.rsc").u8string();
    Check(CreateDataResource(cancelledPath, &error), error);
    const auto scanOwner = workspace.Open("resource.data");
    const auto cancelled = EditorClient(editor.Host(), scanOwner).Command("resource.open-library", {TextArgument("path", cancelledPath)});
    Check(cancelled.status == EDITOR_QUEUED && workspace.Close(scanOwner), "prepare scan owner closure");
    Check(finish(cancelled).status == EDITOR_CANCELLED && workspace.ViewCount() == 2 &&
          client.Query("resource.library", {TextArgument("path", cancelledPath)}).status == EDITOR_STALE,
          "closed view published a background resource scan");
    const auto originalRoot = root / fs::u8path(libraryStem);
    fs::rename(originalRoot, root / "temporarily-missing");
    Check(finish(client.Command("resource.refresh-library", {TextArgument("path", library)})).status == EDITOR_ERROR &&
              page(0).status == EDITOR_OK,
          "failed resource refresh discarded published directory data");
    fs::rename(root / "temporarily-missing", originalRoot);
    std::error_code linkError;
    fs::create_directory_symlink(originalRoot, originalRoot / "cycle", linkError);
    if (!linkError) {
        Check(finish(client.Command("resource.refresh-library", {TextArgument("path", library)})).status == EDITOR_ERROR &&
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
        finish(client.Command("resource.create-library", {TextArgument("path", (root / "Created").u8string())}));
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
