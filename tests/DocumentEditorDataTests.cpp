#include "VirtoolsDevTestSupport.h"

#include <SDL3/SDL_timer.h>
#include <utility>

#include "core/layout/LayoutCommands.h"

namespace vdevtest {
namespace {
struct Folder {
    std::filesystem::path path = std::filesystem::current_path() /
        ("document-editor-data-" + std::to_string(SDL_GetTicksNS()));
    Folder() { CHECK(std::filesystem::create_directory(path)); }
    ~Folder() { std::error_code error; std::filesystem::remove_all(path, error); }
    std::string File(const char *name) const { return (path / name).u8string(); }
};

CK_ID Named(Document &document, const char *name) {
    auto *object = document.Context()->GetObjectByName(const_cast<CKSTRING>(name));
    CHECK(object);
    return object->GetID();
}

void *Picture(CKDWORD pixel) {
    VxImageDescEx image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = image.Height = 1;
    image.BytesPerLine = sizeof(pixel);
    image.Image = reinterpret_cast<XBYTE *>(&pixel);
    void *bitmap = VxCreateBitmap(image);
    CHECK(bitmap);
    return bitmap;
}

CKDWORD Pixel(const std::shared_ptr<void> &bitmap) {
    VxImageDescEx image;
    std::unique_ptr<XBYTE[]> bytes(VxConvertBitmap(bitmap.get(), image));
    CHECK(bytes && image.Width == 1 && image.Height == 1 && image.BitsPerPixel == 32);
    CKDWORD result = 0;
    std::memcpy(&result, bytes.get(), sizeof(result));
    return result | 0xFF000000;
}

class SaveCallback : public CKBaseManager {
public:
    explicit SaveCallback(CKContext *context)
        : CKBaseManager(context, CKGUID(0x29274b11, 0x7c16efb9), "Editor data regression") {}
    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PostSave; }
    CKERROR PostSave() override {
        auto run = std::move(callback);
        callback = {};
        if (run) run();
        return CK_OK;
    }
    std::function<void()> callback;
};
} // namespace

void DocumentLayoutMetadata(CkRuntimeHost &host) {
    Document document(host);
    Folder folder;
    CHECK(document.New(nullptr));
    auto &history = document.History();
    const auto camera = CreateLayoutObject(document, LayoutObject::Camera, 0, 0, {true, true});
    CHECK(camera.ok && history.UndoCount() == 1);
    CHECK(document.Layout().cones.count(camera.created) && document.Layout().playerShown.count(camera.created));
    CHECK(history.Undo());
    CHECK(document.Layout().cones.empty() && document.Layout().playerShown.empty());
    CHECK(history.Redo());
    CHECK(document.Layout().cones.count(camera.created) && document.Layout().playerShown.count(camera.created));

    const auto frame = CreateLayoutObject(document, LayoutObject::Frame3D);
    const auto curve = CreateLayoutObject(document, LayoutObject::Curve);
    CHECK(frame.ok && curve.ok);
    document.Context()->GetObject(frame.created)->SetName(const_cast<CKSTRING>("Metadata Frame"));
    document.Context()->GetObject(curve.created)->SetName(const_cast<CKSTRING>("Metadata Curve"));
    history.Clear();
    document.MarkSaved();
    history.MarkSavePoint();
    CHECK(SetFrameRepresentation(document, {frame.created, frame.created}, 3).ok);
    CHECK(history.UndoCount() == 1 && document.Dirty());
    CHECK(SetFrameRepresentation(document, {frame.created}, 3).Unchanged() && history.UndoCount() == 1);
    CHECK(SetLayoutMembership(document, LayoutList::PlayerShown, frame.created, true, "Show In Player Mode").ok);
    CHECK(SetLayoutMembership(document, LayoutList::HiddenCurvePoints, curve.created, true, "Hide curve points").ok);
    const auto expected = document.Layout();
    const auto steps = history.UndoCount();
    {
        Edit cancelled(document, "Cancel Layout changes");
        CHECK(SetFrameRepresentation(document, {frame.created}, 0).ok);
        CHECK(SetLayoutMembership(document, LayoutList::Cones, camera.created, false, "Hide cone").ok);
    }
    CHECK(document.Layout() == expected && history.UndoCount() == steps);
    CHECK(!SetLayoutMembership(document, LayoutList::Cones, 0, true, "Invalid object").ok);

    // Copying carries the frame's representation in the same undo step.
    std::vector<std::pair<CK_ID, CK_ID>> copies;
    CHECK(DuplicateObjects(document, {frame.created}, &copies).ok && copies.size() == 1);
    const CK_ID copy = copies.front().second;
    CHECK(history.UndoCount() == steps + 1 && document.Layout().representations.at(copy) == 3);
    CHECK(history.Undo() && !document.Layout().representations.count(copy));
    CHECK(history.Redo() && document.Layout().representations.at(copy) == 3);

    SaveReport report;
    CHECK(SaveDocumentAs(document, folder.File("layout.cmo"), {}, report));
    CHECK(report.documentSaved && !document.Dirty());
    CHECK(SetFrameRepresentation(document, {frame.created}, 0).ok);
    const auto beforeOpen = document.Layout();
    const auto beforeHistory = history.UndoCount();
    CHECK(!document.PrepareOpen(folder.File("missing.cmo"), nullptr));
    CHECK(document.Layout() == beforeOpen && history.UndoCount() == beforeHistory);
    auto prepared = document.PrepareOpen(folder.File("layout.cmo"), nullptr);
    CHECK(prepared && document.Layout() == beforeOpen);
    prepared->Commit();
    CHECK(document.Layout().representations.at(Named(document, "Metadata Frame")) == 3);
    CHECK(document.Layout().hiddenCurvePoints.count(Named(document, "Metadata Curve")));
    CHECK(!document.Dirty() && !history.CanUndo());
    CHECK(document.New(nullptr));
    CHECK(document.Layout() == LayoutLists{} && document.Pictures().empty());
}

void DocumentPictures(CkRuntimeHost &host) {
    Document document(host);
    Folder folder;
    CHECK(document.New(nullptr));
    const auto incoming = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(incoming.ok);
    document.Context()->GetObject(incoming.created)->SetName(const_cast<CKSTRING>("Incoming pictured frame"));
    CHECK(SetObjectPicture(document, incoming.created, Picture(0xFF112233)).ok);
    SaveReport incomingReport;
    CHECK(SaveDocumentAs(document, folder.File("incoming.cmo"), {}, incomingReport));
    CHECK(document.New(nullptr));
    auto &history = document.History();
    const auto frame = CreateLayoutObject(document, LayoutObject::Frame3D);
    CHECK(frame.ok);
    document.Context()->GetObject(frame.created)->SetName(const_cast<CKSTRING>("Pictured Frame"));
    history.Clear();
    document.MarkSaved();
    history.MarkSavePoint();
    void *first = Picture(0xFF123456);
    CHECK(SetObjectPicture(document, frame.created, first).ok);
    std::weak_ptr<void> original = document.Pictures().at(frame.created);
    CHECK(document.PicturesPending() && document.Dirty());
    CHECK(history.Undo() && document.Pictures().empty() && !document.Dirty());
    CHECK(history.Redo() && document.Pictures().at(frame.created).get() == first);
    SaveReport report;
    CHECK(SaveDocumentAs(document, folder.File("first.cmo"), {}, report));
    CHECK(report.documentSaved && !document.PicturesPending() && !document.Dirty());

    void *second = Picture(0xFF6789AB);
    CHECK(SetObjectPicture(document, frame.created, second).ok);
    history.Clear();
    CHECK(original.expired());
    const auto steps = history.UndoCount();
    CHECK(!document.Merge(folder.File("missing.cmo"), false, nullptr));
    CHECK(document.PicturesPending() && document.Pictures().at(frame.created).get() == second);
    CHECK(history.UndoCount() == steps && document.Dirty());
    CHECK(SetObjectPicture(document, frame.created, nullptr).ok && document.Pictures().empty());
    CHECK(history.Undo() && Pixel(document.Pictures().at(frame.created)) == 0xFF6789AB);
    CHECK(SaveDocumentAs(document, folder.File("second.cmo"), {}, report));
    CHECK(document.Open(folder.File("second.cmo"), nullptr));
    const CK_ID loaded = Named(document, "Pictured Frame");
    CHECK(Pixel(document.Pictures().at(loaded)) == 0xFF6789AB && !document.PicturesPending());

    // Merging must use current document data for existing objects, including
    // a pending removal whose old pixels still exist in the runtime chunks.
    CHECK(SetObjectPicture(document, loaded, nullptr).ok);
    CHECK(document.Merge(folder.File("incoming.cmo"), false, nullptr));
    CHECK(!document.Pictures().count(loaded));
    const CK_ID incomingId = Named(document, "Incoming pictured frame");
    CHECK(Pixel(document.Pictures().at(incomingId)) == 0xFF112233);
    CHECK(history.Undo() && Pixel(document.Pictures().at(loaded)) == 0xFF6789AB);
    CHECK(document.Pictures().count(incomingId));

    // Native save callbacks may edit the document. A successful publication
    // must not acknowledge the new image or mark that newer document saved.
    auto *callback = new SaveCallback(document.Context());
    CHECK(document.Context()->RegisterNewManager(callback) == CK_OK);
    callback->callback = [&] { CHECK(SetObjectPicture(document, loaded, Picture(0xFFABCDEF)).ok); };
    CHECK(SaveDocumentAs(document, folder.File("callback.cmo"), {}, report));
    CHECK(report.saved && !report.documentSaved && document.Dirty() && document.PicturesPending());
    CHECK(Pixel(document.Pictures().at(loaded)) == 0xFFABCDEF);
    CHECK(document.Open(folder.File("callback.cmo"), nullptr));
    CHECK(Pixel(document.Pictures().at(Named(document, "Pictured Frame"))) == 0xFF6789AB);

    const auto script = CreateScript(document, document.Context()->GetCurrentLevel()->GetID(), "Pictured Script");
    CHECK(script.ok && SetObjectPicture(document, script.created, Picture(0xFF345678)).ok);
    CHECK(SaveDocumentAs(document, folder.File("script.cmo"), {}, report));
    CHECK(DeleteScript(document, script.created).ok && document.PicturesPending());
    CHECK(SaveDocumentAs(document, folder.File("deleted.cmo"), {}, report));
    CHECK(history.Undo() && document.PicturesPending());
    CHECK(SaveDocumentAs(document, folder.File("restored.cmo"), {}, report));
    CHECK(document.Open(folder.File("restored.cmo"), nullptr));
    const CK_ID restored = Named(document, "Pictured Script");
    CHECK(Pixel(document.Pictures().at(restored)) == 0xFF345678);
    std::weak_ptr<void> retired = document.Pictures().at(restored);
    CHECK(DeleteScript(document, restored).ok);
    history.Clear();
    CHECK(!document.Pictures().count(restored) && retired.expired());

    CHECK(document.New(nullptr));
    CHECK(document.Pictures().empty() && !document.PicturesPending());
}
} // namespace vdevtest
