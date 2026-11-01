#include "features/layout/LayoutPresentation.h"
#include "editor/ImGuiDrawing.h"
#include <stdexcept>
#include "features/layout/LayoutViewport.h"
#include "features/layout/SnapshotCommands.h"
#include "features/layout/SnapshotData.h"
#include "application/ObjectSelection.h"
#include "application/Preferences.h"
#include "application/RuntimeOperation.h"
#include "core/document/Document.h"
#include "core/document/ObjectPictures.h"
#include "core/document/SaveFileOperations.h"
#include "core/document/ScriptSession.h"
#include "core/graph/ScriptCommands.h"
#include "core/layout/LayoutCommands.h"
#include "core/layout/LayoutView.h"
#include "core/layout/SnapshotBuffer.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/Bitmaps.h"
#include <fstream>
#include <limits>
#include <thread>

namespace {
using namespace vdev;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Viewport : LayoutViewport {
    int reads = 0, enters = 0, exits = 0;
    bool capture = true, fail = false, activated = true;
    std::vector<unsigned char> image;
    Viewport() : image(104 * 80 * 4) {
        for (size_t i = 0; i < image.size(); i += 4) {
            image[i] = uint8_t(i % 251);
            image[i + 1] = 121;
            image[i + 2] = 53;
            image[i + 3] = 255;
        }
    }
    bool CapturePixels(std::vector<unsigned char> &pixels, int &width, int &height) override {
        ++reads;
        if (!capture)
            return false;
        pixels = image;
        width = 104;
        height = 80;
        return true;
    }
    bool EnterFullScreen(const LayoutScreenMode &, std::string &error) override {
        ++enters;
        if (fail)
            error = "fullscreen failure";
        return !fail;
    }
    bool ExitFullScreen(bool activate, std::string &error) override {
        ++exits;
        activated = activate;
        if (fail)
            error = "fullscreen failure";
        return !fail;
    }
};
struct State {
    Document &doc;
    ObjectSelection &selection;
    DocumentToken token;
    uint64_t change, topology;
    size_t undo;
    bool redo, dirty, locked;
    std::vector<CK_ID> selected;
    State(Document &doc, ObjectSelection &selection)
        : doc(doc), selection(selection), token(doc.Token()), change(doc.ChangeRevision()),
          topology(doc.TopologyRevision()), undo(doc.History().UndoCount()), redo(doc.History().CanRedo()),
          dirty(doc.Dirty()), locked(selection.Locked()), selected(selection.Objects()) {}
    void Unchanged() const {
        Check(doc.Token() == token && doc.ChangeRevision() == change && doc.TopologyRevision() == topology &&
                  doc.History().UndoCount() == undo && doc.History().CanRedo() == redo && doc.Dirty() == dirty &&
                  selection.Objects() == selected && selection.Locked() == locked,
              "presentation changed document, selection or history");
    }
};
EditorValue Real(const char *name, double value) {
    EditorValue result{EditorLiteral(name), EDITOR_REAL_VALUE};
    result.real = value;
    return result;
}
struct FailPublish : SaveFileOperations {
    std::error_code Replace(const std::filesystem::path &, const std::filesystem::path &, bool) override {
        return std::make_error_code(std::errc::permission_denied);
    }
};
std::string Bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
} // namespace

void TestLayoutPresentation(CkRuntimeHost &runtime) {
    Document doc(runtime);
    std::string error;
    Check(doc.New(&error), error.c_str());
    auto first = CreateLayoutObject(doc, LayoutObject::Frame3D);
    auto script = CreateScript(doc, doc.Context()->GetCurrentLevel()->GetID(), "Snapshot Script");
    Check(first.ok && script.ok, "create presentation objects");
    ObjectSelection selection;
    selection.Restore(doc, {first.created}, true);
    const State original(doc, selection);
    LayoutView view(runtime);
    view.CompositionChanged();
    bool available = true, running = false;
    EditorApplication editor([&] { return &doc; }, [&] { return available; });
    int workerFailure = 0;
    RuntimeOperation operations({}, [&](const std::string &, const std::function<void()> &work) {
        if (workerFailure == 1)
            throw std::runtime_error("before bitmap work");
        work();
        if (workerFailure == 2)
            throw std::runtime_error("after bitmap work");
    });
    Viewport viewport;
    Preferences preferences;
    LayoutPresentation presentation(
        editor, [&] { return &doc; }, [&]() -> LayoutView & { return view; }, viewport, selection, preferences,
        [&] { return running; });
    SnapshotCommands snapshots(editor, operations, viewport, [&] { return &doc; });
    Check(presentation.Register(error) && snapshots.Register(error) && editor.Activate(EDITOR_IMGUI_VERSION, error),
          error.c_str());
    auto attach = [&] { editor.Attach(doc.Token(), doc.TopologyRevision(), true, true); };
    attach();
    const EditorClient client(editor.Host(), 0);
    presentation.Poll();
    Check(client.Query("layout.presentation").status == EDITOR_OK &&
              !EditorClient::Integer(client.Query("layout.presentation"), "player"),
          "stopped Player projection");
    Check(client.Command("layout.toggle-fullscreen").status == EDITOR_REFUSED && !viewport.enters,
          "fullscreen without a running viewport");
    Check(client.Command("layout.leave-fullscreen", {IntegerArgument("deactivated", 2)}).status == EDITOR_INVALID &&
              !viewport.exits,
          "invalid fullscreen activation accepted");
    Check(
        client.Query("layout.debug-data", {Real("x", std::numeric_limits<double>::infinity()), Real("y", 0)}).status ==
            EDITOR_INVALID,
        "nonfinite debug position accepted");
    Check(client.Query("layout.debug-data", {Real("x", -1), Real("y", -1)}).status == EDITOR_OK,
          "outside debug query failed");
    running = true;
    doc.Context()->Play();
    presentation.Poll();
    Check(EditorClient::Integer(client.Query("layout.presentation"), "player"), "running Player projection missing");
    running = false;
    presentation.Poll();
    Check(!EditorClient::Integer(client.Query("layout.presentation"), "player") &&
              EditorClient::Integer(client.Query("layout.presentation"), "playing"),
          "paused debugger confused with Player mode");
    doc.Context()->Pause();
    available = false;
    Check(client.Query("layout.presentation").status == EDITOR_REFUSED, "busy native presentation query was accepted");
    Check(client.Query("snapshot.slots").status == EDITOR_OK, "copied snapshot query blocked by native work");
    available = true;
    EditorStatus wrong = EDITOR_OK;
    std::thread([&] { wrong = client.Query("snapshot.slots").status; }).join();
    Check(wrong != EDITOR_OK, "wrong-thread snapshot query accepted");
    std::thread([&] { wrong = client.Query("layout.presentation").status; }).join();
    Check(wrong != EDITOR_OK, "wrong-thread Layout presentation query accepted");
    SnapshotData data;
    Check(ReadSnapshots(client, data) && data.FirstFree() == 0, "empty snapshot projection");
    auto taken = client.Command("snapshot.take");
    Check(taken.status == EDITOR_OK && viewport.reads == 1 && EditorClient::Integer(taken, "slot") == 0,
          "Take did not read exactly once");
    Check(ReadSnapshots(client, data) && data.Has(0) &&
              data.Pixels(0) == ShrinkImage(viewport.image.data(), 104, 80, 416, 52, 40),
          "Take pixels changed");
    const SnapshotDrag firstImage{0, data.Revision(0)};
    Check(client.Command("snapshot.capture", {Real("x", 1e20), Real("y", 0)}).status == EDITOR_INVALID &&
              viewport.reads == 1,
          "invalid Capture read pixels");
    Check(client.Command("snapshot.capture", {Real("x", -4), Real("y", -8)}).status == EDITOR_OK && viewport.reads == 2,
          "Capture did not read exactly once");
    Check(ReadSnapshots(client, data) && data.Pixels(1) == CaptureArea(viewport.image.data(), 104, 80, 416, -4, -8),
          "Capture outside pixels changed");
    const auto revision = data.Revision(1);
    auto stale = data;
    stale.slots[1].revision++;
    Check(RemoveSnapshots(client, stale, {0, 1}).status == EDITOR_STALE && ReadSnapshots(client, data) && data.Has(0) &&
              data.Has(1),
          "batch deletion partially changed snapshots");
    const auto reads = viewport.reads;
    viewport.capture = false;
    Check(client.Command("snapshot.take").status == EDITOR_REFUSED && ReadSnapshots(client, data) &&
              data.FirstFree() == 2,
          "failed read changed snapshots");
    viewport.capture = true;
    std::vector<unsigned char> savedPixels;
    Check(ReadSnapshot(client, firstImage, savedPixels), "snapshot pixels query failed");
    Check(viewport.reads == reads + 1, "snapshot queries performed a readback");
    Check(RemoveSnapshots(client, data, {0}).status == EDITOR_OK && client.Command("snapshot.take").status == EDITOR_OK,
          "reuse snapshot slot");
    std::vector<unsigned char> unchanged{1, 2, 3};
    Check(!ReadSnapshot(client, firstImage, unchanged) && unchanged == std::vector<unsigned char>({1, 2, 3}),
          "old drag resolved to replacement image");
    Check(ApplySnapshot(client, firstImage, doc.Reference(first.created), false).status == EDITOR_STALE,
          "stale drag changed document");
    original.Unchanged();
    Check(ReadSnapshots(client, data), "refresh snapshots");
    const SnapshotDrag currentImage{0, data.Revision(0)};
    Check(ApplySnapshot(client, currentImage, doc.Reference(first.created), false).status == EDITOR_OK &&
              doc.History().UndoCount() == original.undo + 1,
          "object snapshot is not one Undo");
    Check(doc.History().Undo(), "undo object picture");
    attach();
    const auto beforeScript = doc.History().UndoCount();
    Check(ApplySnapshot(client, currentImage, doc.Reference(script.created), true).status == EDITOR_OK &&
              doc.History().UndoCount() == beforeScript + 1,
          "Script snapshot is not one Undo");
    Check(doc.History().Undo(), "undo Script picture");
    attach();
    const auto history = doc.History().UndoCount();
    const bool redo = doc.History().CanRedo();
    const auto dir = std::filesystem::temp_directory_path() / "nemodev-snapshot-commands";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto destination = dir / "image.bmp";
    auto file = [&](const char *command, int slot, const char *choice, const std::string &path = {}) {
        Check(ReadSnapshots(client, data), "refresh file snapshot versions");
        const auto result =
            client.Command(command, {IntegerArgument("slot", slot), IntegerArgument("revision", data.Revision(slot))});
        Check(result.status == EDITOR_QUEUED, "snapshot file command did not queue");
        const auto input = client.Query("snapshot.input");
        Check(input.status == EDITOR_OK, "snapshot file input missing");
        const auto id = EditorClient::Integer(input, "input");
        Check(client.Command("snapshot.respond",
                             {IntegerArgument("input", id), TextArgument("choice", choice), TextArgument("path", path)})
                      .status == EDITOR_OK,
              "snapshot file answer failed");
        return std::pair<EditorOperation, uint64_t>{result.operation, id};
    };
    auto cancelled = file("snapshot.load", 2, "cancel");
    Check(client.Completion(cancelled.first).status == EDITOR_CANCELLED && !operations.Busy(),
          "cancelled bitmap load started work");
    Check(client.Command("snapshot.respond", {IntegerArgument("input", cancelled.second),
                                              TextArgument("choice", "accept"), TextArgument("path", "late.bmp")})
                  .status == EDITOR_STALE,
          "late bitmap response accepted");
    auto saved = file("snapshot.save", 0, "accept", destination.u8string());
    snapshots.Poll();
    operations.Finish();
    Check(client.Completion(saved.first).status == EDITOR_OK && std::filesystem::exists(destination),
          "snapshot bitmap save failed");
    const auto oldBytes = Bytes(destination);
    Check(oldBytes.size() > 54, "saved bitmap empty");
    FailPublish failedFiles;
    snapshots.FileOperations(&failedFiles);
    auto failed = file("snapshot.save", 1, "accept", destination.u8string());
    snapshots.Poll();
    operations.Finish();
    Check(client.Completion(failed.first).status == EDITOR_ERROR && Bytes(destination) == oldBytes,
          "failed bitmap publish replaced original file");
    snapshots.FileOperations(nullptr);
    auto load = file("snapshot.load", 2, "accept", destination.u8string());
    snapshots.Poll();
    operations.DocumentChanged();
    operations.Finish();
    Check(client.Completion(load.first).status == EDITOR_OK && ReadSnapshots(client, data) && data.Has(2),
          "application bitmap load expired with the document");
    auto bad = file("snapshot.load", 3, "accept", (dir / "missing.bmp").u8string());
    snapshots.Poll();
    operations.Finish();
    Check(client.Completion(bad.first).status == EDITOR_ERROR && ReadSnapshots(client, data) && !data.Has(3),
          "failed load changed buffer");
    for (int failure : {1, 2}) {
        workerFailure = failure;
        auto rejected = file("snapshot.load", 3, "accept", destination.u8string());
        snapshots.Poll();
        operations.Finish();
        Check(client.Completion(rejected.first).status == EDITOR_ERROR && ReadSnapshots(client, data) && !data.Has(3),
              "bitmap worker exception changed buffer");
    }
    workerFailure = 1;
    auto writeException = file("snapshot.save", 1, "accept", destination.u8string());
    snapshots.Poll();
    operations.Finish();
    Check(client.Completion(writeException.first).status == EDITOR_ERROR && Bytes(destination) == oldBytes,
          "bitmap exception overwrote original file");
    workerFailure = 2;
    auto published = file("snapshot.save", 1, "accept", destination.u8string());
    snapshots.Poll();
    operations.Finish();
    const auto publishedResult = client.Completion(published.first);
    Check(publishedResult.status == EDITOR_OK && !publishedResult.message.empty(),
          "published bitmap lost its saved result after worker exception");
    workerFailure = 0;
    const auto sourcePixels = data.Pixels(0);
    auto fullSlot = file("snapshot.load", 0, "accept", destination.u8string());
    snapshots.Poll();
    operations.Finish();
    Check(client.Completion(fullSlot.first).status == EDITOR_OK && ReadSnapshots(client, data) && data.Has(3) &&
              data.Pixels(0) == sourcePixels,
          "loading a full slot replaced its image");
    Check(doc.History().UndoCount() == history && doc.History().CanRedo() == redo &&
              selection.Objects() == original.selected && selection.Locked() == original.locked,
          "bitmap file operations changed selection or history");
    Check(data.Revision(1) == revision, "unrelated snapshot slot changed");
    // Object pictures share the asynchronous bitmap input, but stay tied to
    // their original object and Setup scope rather than the Snapshot Buffer.
    const auto pictureTarget = doc.Reference(first.created);
    ObjectPictureData picture;
    Check(ReadObjectPicture(client, pictureTarget, picture) && !picture.present, "empty object picture query");
    const auto imagePath = (dir / "full-picture.bmp").u8string();
    auto *fullBitmap = BitmapFromPixels(viewport.image.data(), 104, 80);
    Check(fullBitmap && CKSaveBitmap(const_cast<CKSTRING>(imagePath.c_str()), fullBitmap), "save full picture fixture");
    VxDeleteBitmap(fullBitmap);
    const auto loadPicture = [&](const EditorClient &source, ObjectRef target, const char *choice,
                                 const std::string &path = std::string()) {
        const auto load = source.Command("object.load-picture", {ObjectArgument("target", target)});
        Check(load.status == EDITOR_QUEUED, "object picture load did not queue");
        const auto input = source.Query("snapshot.input");
        Check(input.status == EDITOR_OK && EditorClient::Text(input, "title") == "Load Snapshot",
              "object picture did not reuse file input");
        Check(source.Command("snapshot.respond", {IntegerArgument("input", EditorClient::Integer(input, "input")),
                                                  TextArgument("choice", choice), TextArgument("path", path)})
                      .status == EDITOR_OK,
              "answer object picture input");
        return load.operation;
    };
    const auto pictureReads = viewport.reads;
    const State beforePictures(doc, selection);
    const auto cancelledPicture = loadPicture(client, pictureTarget, "cancel");
    Check(client.Completion(cancelledPicture).status == EDITOR_CANCELLED && !operations.Busy(),
          "cancelled object picture load started work");
    const auto missingPicture = loadPicture(client, pictureTarget, "accept", (dir / "missing.bmp").u8string());
    snapshots.Poll();
    operations.Finish();
    Check(client.Completion(missingPicture).status == EDITOR_ERROR && doc.Pictures().empty(),
          "failed object picture load changed document");
    beforePictures.Unchanged();
    const auto loadedPicture = loadPicture(client, pictureTarget, "accept", imagePath);
    snapshots.Poll();
    operations.Finish();
    attach();
    Check(client.Completion(loadedPicture).status == EDITOR_OK &&
              doc.History().UndoCount() == beforePictures.undo + 1 &&
              ReadObjectPicture(client, pictureTarget, picture) && picture.present &&
              picture.pixels == ShrinkImage(viewport.image.data(), 104, 80, 416, 52, 40),
          "object picture load lost pixels or split Undo");
    std::vector<unsigned char> fullPixels;
    int pictureWidth = 0, pictureHeight = 0;
    Check(PixelsOfBitmap(doc.Pictures().at(first.created).get(), fullPixels, pictureWidth, pictureHeight) &&
              pictureWidth == 104 && pictureHeight == 80 && fullPixels == viewport.image,
          "object picture load replaced stored image with a thumbnail");
    const auto imageRevision = picture.Revision();
    Check(ReadObjectPicture(client, pictureTarget, picture) && picture.Revision() == imageRevision,
          "unchanged picture invalidated its UI texture");
    doc.MarkTopologyChanged();
    attach();
    Check(ReadObjectPicture(client, pictureTarget, picture) && picture.Revision() == imageRevision,
          "unrelated document update invalidated picture pixels");
    const State loadedState(doc, selection);
    for (int failure : {1, 2}) {
        workerFailure = failure;
        const auto rejectedPicture = loadPicture(client, pictureTarget, "accept", imagePath);
        snapshots.Poll();
        operations.Finish();
        Check(client.Completion(rejectedPicture).status == EDITOR_ERROR &&
                  ReadObjectPicture(client, pictureTarget, picture) && picture.Revision() == imageRevision,
              "bitmap worker exception replaced an object picture");
        loadedState.Unchanged();
    }
    workerFailure = 0;
    const auto pictureScope = editor.OpenScope();
    const EditorClient scoped(editor.Host(), pictureScope);
    const auto closedScope = loadPicture(scoped, pictureTarget, "accept", imagePath);
    snapshots.Poll();
    editor.CloseScope(pictureScope);
    operations.Finish();
    Check(client.Completion(closedScope).status == EDITOR_STALE, "closed Setup received a loaded picture");
    loadedState.Unchanged();
    Check(client.Command("object.remove-picture", {ObjectArgument("target", pictureTarget)}).status == EDITOR_OK &&
              doc.History().UndoCount() == loadedState.undo + 1 && doc.Pictures().empty() && doc.History().Undo(),
          "object picture removal is not one reversible edit");
    attach();
    Check(ReadObjectPicture(client, pictureTarget, picture) && picture.present && doc.History().Undo(),
          "picture load Undo failed");
    attach();
    Check(ReadObjectPicture(client, pictureTarget, picture) && !picture.present && doc.History().Redo(),
          "picture load Redo failed");
    attach();
    Check(ReadObjectPicture(client, pictureTarget, picture) && picture.present && viewport.reads == pictureReads,
          "object pictures used viewport readback or lost their image");
    const auto copiedPicture = picture;
    auto stalePicture = pictureTarget;
    ++stalePicture.lifetime;
    Check(!ReadObjectPicture(client, stalePicture, picture) && picture.target == copiedPicture.target &&
              picture.pixels == copiedPicture.pixels &&
              client.Command("object.remove-picture", {ObjectArgument("target", stalePicture)}).status == EDITOR_STALE,
          "stale picture target changed document or previous projection");
    auto *retired = doc.Context()->CreateObject(CKCID_OBJECT, "Retired picture target");
    Check(retired != nullptr, "create retired picture target");
    const auto retiredId = retired->GetID();
    doc.Include(retiredId);
    const auto retiredRef = doc.Reference(retiredId);
    Check(SetObjectPicture(doc, retiredId, BitmapFromPixels(viewport.image.data(), 104, 80)).ok && doc.History().Undo(),
          "prepare picture history for reused ID");
    attach();
    const auto retiredLoad = loadPicture(client, retiredRef, "accept", imagePath);
    snapshots.Poll();
    Check(doc.Context()->DestroyObject(retiredId, CK_DESTROY_FREEID) == CK_OK, "destroy picture target");
    auto *replacement = doc.Context()->CreateObject(CKCID_OBJECT, "Replacement picture target");
    Check(replacement && replacement->GetID() == retiredId, "reuse picture target ID");
    doc.Include(replacement->GetID());
    operations.Finish();
    Check(client.Completion(retiredLoad).status == EDITOR_STALE && !doc.Pictures().count(retiredId) &&
              !doc.History().Redo() && !doc.Pictures().count(retiredId),
          "loaded or redone picture followed a reused native ID");
    attach();
    const auto obsoletePicture = loadPicture(client, pictureTarget, "accept", imagePath);
    snapshots.Poll();
    const auto oldTarget = doc.Reference(first.created);
    Check(doc.New(&error), error.c_str());
    attach();
    operations.DocumentChanged();
    operations.Finish();
    Check(client.Completion(obsoletePicture).status == EDITOR_STALE && doc.Pictures().empty() &&
              !doc.History().CanUndo(),
          "old document picture load changed the replacement document");
    presentation.Poll();
    Check(ReadSnapshots(client, data) && data.Has(0) && data.Has(2),
          "document replacement cleared application snapshots");
    Check(ApplySnapshot(client, currentImage, oldTarget, false).status != EDITOR_OK,
          "old-document snapshot target accepted");
    while (ReadSnapshots(client, data) && data.FirstFree() >= 0)
        Check(client.Command("snapshot.take").status == EDITOR_OK, "fill snapshot buffer");
    const auto fullReads = viewport.reads;
    Check(client.Command("snapshot.take").status == EDITOR_REFUSED && viewport.reads == fullReads,
          "full snapshot buffer read pixels");
    snapshots.Stop();
    std::filesystem::remove_all(dir);
    view.Stop();
    doc.Close();
}
