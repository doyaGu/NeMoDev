#include "core/level/SetupCommands.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <set>

#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/level/ObjectEditCommands.h"
#include "core/level/detail/LevelCommandsDetail.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/GridCompat.h"

namespace vdev {

namespace {

int SlotCount(CKObject *object) {
    if (CKTexture *texture = CKTexture::Cast(object)) return texture->GetSlotCount();
    if (CKSprite *sprite = CKSprite::Cast(object)) return sprite->GetSlotCount();
    return 0;
}

// A texture's or a sprite's images (CKSprite is not a CKBitmapData).
struct Images {
    CKTexture *texture = nullptr;
    CKSprite *sprite = nullptr;

    explicit Images(CKObject *object) : texture(CKTexture::Cast(object)), sprite(CKSprite::Cast(object)) {}
    explicit operator bool() const { return texture || sprite; }
    int Slots() const { return texture ? texture->GetSlotCount() : sprite->GetSlotCount(); }
    int Current() const { return texture ? texture->GetCurrentSlot() : sprite->GetCurrentSlot(); }
    void SetSlots(int count) const { texture ? texture->SetSlotCount(count) : sprite->SetSlotCount(count); }
    void SetCurrent(int slot) const { texture ? texture->SetCurrentSlot(slot) : sprite->SetCurrentSlot(slot); }
    int Width() const { return texture ? texture->GetWidth() : sprite->GetWidth(); }
    int Height() const { return texture ? texture->GetHeight() : sprite->GetHeight(); }
    CKBYTE *Lock(int slot) const { return texture ? texture->LockSurfacePtr(slot) : sprite->LockSurfacePtr(slot); }
    void Release(int slot) const { texture ? texture->ReleaseSurfacePtr(slot) : sprite->ReleaseSurfacePtr(slot); }
    const char *File(int slot) const { return texture ? texture->GetSlotFileName(slot) : sprite->GetSlotFileName(slot); }
    void SetFile(int slot, const std::string &name) const {
        CKSTRING file = name.empty() ? nullptr : const_cast<CKSTRING>(name.c_str());
        texture ? texture->SetSlotFileName(slot, file) : sprite->SetSlotFileName(slot, file);
    }
    bool Create(int width, int height, int slot) const {
        return (texture ? texture->Create(width, height, 32, slot) : sprite->Create(width, height, 32, slot)) != FALSE;
    }
    bool Load(const std::string &path, int slot) const {
        CKSTRING name = const_cast<CKSTRING>(path.c_str());
        return (texture ? texture->LoadImage(name, slot) : sprite->LoadImage(name, slot)) != FALSE;
    }
    bool Movie() const {
        const char *movie = texture ? texture->GetMovieFileName() : sprite->GetMovieFileName();
        return movie && *movie;
    }
};

// Every slot's image (32-bit ARGB), as an undo step keeps them.
struct ImagesState {
    int width = 0, height = 0, current = 0;
    std::vector<std::vector<CKBYTE>> pixels;
    std::vector<std::string> files;

    bool operator==(const ImagesState &other) const {
        return width == other.width && height == other.height && current == other.current && pixels == other.pixels &&
               files == other.files;
    }
};

ImagesState Take(const Images &images) {
    ImagesState state;
    state.width = images.Width();
    state.height = images.Height();
    state.current = images.Current();
    const size_t size = static_cast<size_t>(std::max(0, state.width)) * std::max(0, state.height) * 4;
    for (int slot = 0; slot < images.Slots(); ++slot) {
        std::vector<CKBYTE> copy;
        if (CKBYTE *pixels = size ? images.Lock(slot) : nullptr) {
            copy.assign(pixels, pixels + size);
            images.Release(slot);
        }
        state.pixels.push_back(std::move(copy));
        const char *file = images.File(slot);
        state.files.push_back(file ? file : "");
    }
    return state;
}

bool Put(const Images &images, const ImagesState &state) {
    const int slots = static_cast<int>(state.pixels.size());
    // The slots share one size: another one is made on a single slot first.
    if (images.Width() != state.width || images.Height() != state.height) {
        images.SetSlots(1);
        if (!images.Create(state.width, state.height, 0)) return false;
    }
    images.SetSlots(std::max(1, slots));
    for (int slot = 0; slot < slots; ++slot) {
        const std::vector<CKBYTE> &pixels = state.pixels[slot];
        if (!pixels.empty()) {
            CKBYTE *surface = images.Lock(slot);
            if (!surface && images.Create(state.width, state.height, slot)) surface = images.Lock(slot);
            if (!surface) return false;
            std::memcpy(surface, pixels.data(), pixels.size());
            images.Release(slot);
        }
        images.SetFile(slot, state.files[slot]);
    }
    images.SetCurrent(std::min(std::max(0, state.current), std::max(0, slots - 1)));
    return true;
}

// change on the object's images as one undo step (the images before and
// after); change false: refused with failure, the images put back.
EditResult EditImages(Document &document, CK_ID id, const std::string &label, const std::string &failure,
                      const std::function<bool(const Images &)> &change) {
    CKContext *context = document.Context();
    if (!context || document.IsDiscarded(id)) return EditResult::Refused("The image object is gone");
    const Images images(context->GetObject(id));
    if (!images) return EditResult::Refused("Not a texture or a sprite");
    if (images.Movie()) return EditResult::Refused("Cannot modify avi");
    const ImagesState before = Take(images);
    bool changed = false;
    GuardedCall([&] { changed = change(images); }, &document.Host(), label.c_str());
    if (!changed) {
        Put(images, before);
        return EditResult::Refused(failure);
    }
    const ImagesState after = Take(images);
    if (after == before) return EditResult::NoChange("The images are unchanged");
    Edit edit(document, label);
    auto put = [context, id](const ImagesState &state) {
        return [context, id, state] {
            const Images found(context->GetObject(id));
            return found && Put(found, state);
        };
    };
    edit.Done(put(after), put(before));
    return edit.Commit();
}

// The current slot's pixels (32 bits), changed in place.
bool EachPixel(const Images &images, const std::function<void(CKBYTE *)> &change) {
    const int slot = images.Current(), count = images.Width() * images.Height();
    CKBYTE *pixels = count > 0 ? images.Lock(slot) : nullptr;
    if (!pixels) return false;
    for (int i = 0; i < count; ++i) change(pixels + 4 * i);
    images.Release(slot);
    return true;
}

// The current slot as a VxImageDescEx (32-bit ARGB) for VxMath.
bool ConvertSlot(const Images &images, bool normal) {
    const int slot = images.Current();
    VxImageDescEx desc;
    desc.Width = images.Width();
    desc.Height = images.Height();
    desc.BitsPerPixel = 32;
    desc.BytesPerLine = desc.Width * 4;
    desc.AlphaMask = 0xFF000000;
    desc.RedMask = 0x00FF0000;
    desc.GreenMask = 0x0000FF00;
    desc.BlueMask = 0x000000FF;
    desc.Image = desc.Width > 0 && desc.Height > 0 ? images.Lock(slot) : nullptr;
    if (!desc.Image) return false;
    const bool done = (normal ? VxConvertToNormalMap(desc, 0xFFFFFFFF) : VxConvertToBumpMap(desc)) != FALSE;
    images.Release(slot);
    return done;
}

} // namespace

EditResult EditSlotImage(Document &document, CK_ID bitmap, SlotImageOp op) {
    static const char *const LABELS[] = {"Invert Slot",           "HeightMap => NormalMap",     "HeightMap => BumpMap",
                                         "Set alpha to opaque",   "Set alpha to translucent",   "Set alpha to transparent",
                                         "Invert alpha"};
    const char *label = LABELS[static_cast<int>(op)];
    return EditImages(document, bitmap, label, std::string(label) + " failed", [op](const Images &images) {
        switch (op) {
        case SlotImageOp::InvertColors:
            return EachPixel(images, [](CKBYTE *p) {
                p[0] = static_cast<CKBYTE>(255 - p[0]);
                p[1] = static_cast<CKBYTE>(255 - p[1]);
                p[2] = static_cast<CKBYTE>(255 - p[2]);
            });
        case SlotImageOp::NormalMap: return ConvertSlot(images, true);
        case SlotImageOp::BumpMap: return ConvertSlot(images, false);
        case SlotImageOp::AlphaOpaque: return EachPixel(images, [](CKBYTE *p) { p[3] = 255; });
        case SlotImageOp::AlphaTranslucent: return EachPixel(images, [](CKBYTE *p) { p[3] = 128; });
        case SlotImageOp::AlphaTransparent: return EachPixel(images, [](CKBYTE *p) { p[3] = 0; });
        case SlotImageOp::InvertAlpha: return EachPixel(images, [](CKBYTE *p) { p[3] = static_cast<CKBYTE>(255 - p[3]); });
        }
        return false;
    });
}

EditResult AddSlotImages(Document &document, CK_ID bitmap, const std::vector<std::string> &paths) {
    if (paths.empty()) return EditResult::NoChange("No file");
    return EditImages(document, bitmap, "Add new slot", "Cannot add slot (check texture's size and file)",
                      [&paths](const Images &images) {
                          int slots = images.Slots();
                          for (const std::string &path : paths) {
                              images.SetSlots(slots + 1);
                              if (!images.Load(path, slots)) return false;
                              ++slots;
                          }
                          images.SetCurrent(slots - 1);
                          return true;
                      });
}

EditResult ReplaceSlotImage(Document &document, CK_ID bitmap, const std::string &path, int slot) {
    return EditImages(document, bitmap, "Replace slot",
                      "Cannot replace current texture! (problem while reading the picture file)",
                      [&path, slot](const Images &images) {
                          const int target = slot < 0 ? images.Current() : slot;
                          if (target < 0 || target >= images.Slots()) return false;
                          const int current = images.Current();
                          const bool loaded = images.Load(path, target);
                          if (images.Current() != current) images.SetCurrent(current);
                          return loaded && images.Current() == current;
                      });
}

EditResult ReloadSlotImage(Document &document, CK_ID bitmap) {
    return EditImages(document, bitmap, "Reload slot", "Cannot reload the slot (no file or the file cannot be read)",
                      [](const Images &images) {
                          const char *file = images.File(images.Current());
                          if (!file || !*file) return false;
                          const std::string path = file;
                          return images.Load(path, images.Current());
                      });
}

EditResult ResizeSlotImages(Document &document, CK_ID texture, int width, int height) {
    if (!CKTexture::Cast(document.Context()->GetObject(texture))) return EditResult::Refused("Not a texture");
    if (width <= 0 || height <= 0) return EditResult::Refused("The size must be positive");
    return EditImages(document, texture, "Resize Slot", "Resize Slot failed", [width, height](const Images &images) {
        return images.texture->ResizeImages(width, height) != FALSE;
    });
}

EditResult ReplaceSlotAlpha(Document &document, CK_ID texture, const std::string &path, int channels, int slot) {
    CKContext *context = document.Context();
    if (!CKTexture::Cast(context->GetObject(texture))) return EditResult::Refused("Not a texture");
    // The image is read by a texture of its own (Dev: a temporary CKTexture).
    CKTexture *source = CKTexture::Cast(context->CreateObject(CKCID_TEXTURE, nullptr, CK_OBJECTCREATION_DYNAMIC));
    if (!source) return EditResult::Error("Cannot create a texture");
    std::vector<CKBYTE> image;
    int sw = 0, sh = 0;
    GuardedCall(
        [&] {
            if (!source->LoadImage(const_cast<CKSTRING>(path.c_str()), 0)) return;
            sw = source->GetWidth();
            sh = source->GetHeight();
            if (CKBYTE *pixels = sw > 0 && sh > 0 ? source->LockSurfacePtr(0) : nullptr) {
                image.assign(pixels, pixels + static_cast<size_t>(sw) * sh * 4);
                source->ReleaseSurfacePtr(0);
            }
        },
        &document.Host(), "CKTexture::LoadImage");
    context->DestroyObject(source);
    if (image.empty())
        return EditResult::Refused("Cannot replace current alpha channel! (problem while reading the picture file)");
    const bool blue = (channels & 2) != 0, green = (channels & 4) != 0, red = (channels & 8) != 0;
    const int used = blue + green + red;
    if (!used) return EditResult::Refused("No channel chosen");
    return EditImages(document, texture, "Replace alpha", "Replace alpha failed", [&](const Images &images) {
        const int w = images.Width(), h = images.Height(), target = slot < 0 ? images.Current() : slot;
        CKBYTE *pixels = w > 0 && h > 0 && target >= 0 && target < images.Slots() ? images.Lock(target) : nullptr;
        if (!pixels) return false;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const CKBYTE *from = &image[(static_cast<size_t>(y * sh / h) * sw + x * sw / w) * 4];
                const int sum = (blue ? from[0] : 0) + (green ? from[1] : 0) + (red ? from[2] : 0);
                pixels[(static_cast<size_t>(y) * w + x) * 4 + 3] = static_cast<CKBYTE>((sum + used / 2) / used);
            }
        images.Release(target);
        return true;
    });
}

// The image is given to the bitmap reader of the file's extension (what
// CKBitmapData::SaveImage does; the 2.1 runtime's crashes on a texture made
// with Create); the alpha channel is saved as a gray image (SaveImageAlpha).
bool SaveSlotImage(CKObject *object, const std::string &path, bool alpha, int slot) {
    const Images images(object);
    if (!images || (alpha && !images.texture)) return false;
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) return false;
    if (slot < 0) slot = images.Current();
    if (slot < 0 || slot >= images.Slots()) return false;
    const int w = images.Width(), h = images.Height();
    CKBYTE *surface = w > 0 && h > 0 ? images.Lock(slot) : nullptr;
    if (!surface) return false;
    std::vector<CKBYTE> pixels(surface, surface + static_cast<size_t>(w) * h * 4);
    images.Release(slot);
    if (alpha)
        for (size_t i = 0; i < pixels.size(); i += 4) pixels[i] = pixels[i + 1] = pixels[i + 2] = pixels[i + 3];
    CKFileExtension extension(const_cast<char *>(path.c_str() + dot + 1));
    CKBitmapReader *reader = CKGetPluginManager()->GetBitmapReader(extension);
    if (!reader) return false;
    CKBitmapProperties *defaults = nullptr;
    reader->GetBitmapDefaultProperties(&defaults);
    CKBitmapProperties properties;
    if (defaults) properties = *defaults;
    VxImageDescEx &desc = properties.m_Format;
    desc.Width = w;
    desc.Height = h;
    desc.BitsPerPixel = 32;
    desc.BytesPerLine = w * 4;
    desc.AlphaMask = 0xFF000000;
    desc.RedMask = 0x00FF0000;
    desc.GreenMask = 0x0000FF00;
    desc.BlueMask = 0x000000FF;
    desc.Image = pixels.data();
    properties.m_Data = pixels.data();
    const bool saved = reader->SaveFile(const_cast<char *>(path.c_str()), &properties) != 0;
    reader->Release();
    return saved;
}

EditResult ResetWorldMatrix(Document &document, CK_ID entity) {
    if (!CK3dEntity::Cast(document.Context()->GetObject(entity))) return EditResult::Refused("Not a 3D entity");
    return EditObjectState(document, entity, "Reset World Matrix",
                           [](CKObject *o) { CK3dEntity::Cast(o)->SetWorldMatrix(VxMatrix::Identity()); });
}

EditResult SetScaleToUnit(Document &document, CK_ID entity) {
    if (!CK3dEntity::Cast(document.Context()->GetObject(entity))) return EditResult::Refused("Not a 3D entity");
    return EditObjectState(document, entity, "Set as Unit",
                           [](CKObject *o) { CK3dEntity::Cast(o)->SetScale3f(1, 1, 1, FALSE, FALSE); });
}

EditResult ConsolidateMesh(Document &document, CK_ID mesh) {
    if (!CKMesh::Cast(document.Context()->GetObject(mesh))) return EditResult::Refused("Not a mesh");
    return EditObjectState(document, mesh, "Consolidate", [](CKObject *o) { CKMesh::Cast(o)->Consolidate(); });
}

EditResult AddBitmapSlot(Document &document, CK_ID texture) {
    if (!CKTexture::Cast(document.Context()->GetObject(texture))) return EditResult::Refused("Not a texture");
    return EditObjectState(document, texture, "Add Slot", [](CKObject *o) {
        CKTexture *t = CKTexture::Cast(o);
        const int slots = t->GetSlotCount();
        t->SetSlotCount(slots + 1);
        t->SetCurrentSlot(slots);
    });
}

EditResult RemoveCurrentSlot(Document &document, CK_ID bitmap) {
    CKObject *object = document.Context()->GetObject(bitmap);
    if (SlotCount(object) <= 1) return EditResult::Refused("The last slot cannot be removed");
    return EditObjectState(document, bitmap, "Remove Current Slot", [](CKObject *o) {
        if (CKTexture *t = CKTexture::Cast(o)) {
            const int slot = t->GetCurrentSlot();
            t->ReleaseSlot(slot);
            t->SetCurrentSlot(std::max(0, slot - 1));
        } else if (CKSprite *s = CKSprite::Cast(o)) {
            const int slot = s->GetCurrentSlot();
            s->ReleaseSlot(slot);
            s->SetCurrentSlot(std::max(0, slot - 1));
        }
    });
}

namespace {

CKDataArray *ArrayOf(Document &document, CK_ID id) { return CKDataArray::Cast(document.Context()->GetObject(id)); }

bool ValidColumn(CKDataArray *array, int column) { return array && column >= 0 && column < array->GetColumnCount(); }

} // namespace

bool SortableColumn(CKDataArray *array, int column) {
    if (!ValidColumn(array, column)) return false;
    const CK_ARRAYTYPE type = array->GetColumnType(column);
    return type == CKARRAYTYPE_INT || type == CKARRAYTYPE_FLOAT || type == CKARRAYTYPE_STRING;
}

EditResult SortArray(Document &document, CK_ID id, int column, bool ascending) {
    CKDataArray *array = ArrayOf(document, id);
    if (!ValidColumn(array, column)) return EditResult::Refused("No such column");
    // Documentation "Array Setup": "It only works with integer, float or string basic types".
    if (!SortableColumn(array, column)) return EditResult::Refused("Only integer, float and string columns can be sorted");
    return EditObjectState(document, id, ascending ? "Sort Ascending" : "Sort Descending",
                           [column, ascending](CKObject *o) { CKDataArray::Cast(o)->Sort(column, ascending); });
}

EditResult SetArrayKeyColumn(Document &document, CK_ID id, int column) {
    CKDataArray *array = ArrayOf(document, id);
    if (!array || (column != -1 && !ValidColumn(array, column))) return EditResult::Refused("No such column");
    if (array->GetKeyColumn() == column) return EditResult::NoChange("The key column is unchanged");
    return EditObjectState(document, id, column < 0 ? "Remove Key Column" : "Make Key Column",
                           [column](CKObject *o) { CKDataArray::Cast(o)->SetKeyColumn(column); });
}

EditResult MoveArrayColumn(Document &document, CK_ID id, int column, int step) {
    CKDataArray *array = ArrayOf(document, id);
    const int to = column + step;
    if (!ValidColumn(array, column) || !ValidColumn(array, to) || step == 0)
        return EditResult::Refused("The column cannot move further");
    const int count = array->GetColumnCount();
    return EditObjectState(document, id, step < 0 ? "Move Column Left" : "Move Column Right",
                           [column, step, count](CKObject *o) {
                               CKDataArray *a = CKDataArray::Cast(o);
                               // MoveColumn(source, destination) puts the source before the destination (-1: last).
                               if (step < 0) a->MoveColumn(column, column - 1);
                               else if (column + 1 == count - 1) a->MoveColumn(column, -1);
                               else a->MoveColumn(column + 1, column);
                           });
}

EditResult EditArrayColumn(Document &document, CK_ID id, int column, const std::string &name, CK_ARRAYTYPE type,
                           const CKGUID &parameter) {
    CKDataArray *array = ArrayOf(document, id);
    if (!ValidColumn(array, column)) return EditResult::Refused("No such column");
    if (type == CKARRAYTYPE_PARAMETER && parameter == CKGUID(0, 0)) return EditResult::Refused("Choose a parameter type");
    return EditObjectState(document, id, "Edit Column Properties", [=](CKObject *o) {
        CKDataArray *a = CKDataArray::Cast(o);
        if (a->GetColumnType(column) != type || (type == CKARRAYTYPE_PARAMETER && a->GetColumnParameterGuid(column) != parameter))
            a->SetColumnType(column, type, parameter);
        a->SetColumnName(column, const_cast<char *>(name.c_str()));
    });
}

EditResult RemoveArrayRows(Document &document, CK_ID id, const std::vector<int> &rows) {
    CKDataArray *array = ArrayOf(document, id);
    if (!array || rows.empty()) return EditResult::Refused("Select rows");
    std::vector<int> sorted(rows);
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    if (sorted.front() < 0 || sorted.back() >= array->GetRowCount()) return EditResult::Refused("No such row");
    return EditObjectState(document, id, "Delete Row", [sorted](CKObject *o) {
        for (auto row = sorted.rbegin(); row != sorted.rend(); ++row) CKDataArray::Cast(o)->RemoveRow(*row);
    });
}

EditResult DuplicateArrayRows(Document &document, CK_ID id, const std::vector<int> &rows) {
    CKDataArray *array = ArrayOf(document, id);
    if (!array || rows.empty()) return EditResult::Refused("Select rows");
    for (int row : rows)
        if (row < 0 || row >= array->GetRowCount()) return EditResult::Refused("No such row");
    return EditObjectState(document, id, "Duplicate Row", [rows](CKObject *o) {
        CKDataArray *a = CKDataArray::Cast(o);
        const int columns = a->GetColumnCount();
        for (int row : rows) {
            a->InsertRow(-1);
            const int copy = a->GetRowCount() - 1;
            // Strings and parameters are values of their own (Dev: CKStrdup, CopyValue).
            for (int c = 0; c < columns; ++c) {
                switch (a->GetColumnType(c)) {
                case CKARRAYTYPE_STRING: {
                    const int size = a->GetElementStringValue(row, c, nullptr, 0);
                    std::string text(static_cast<size_t>(std::max(size, 1)), '\0');
                    if (size > 0) a->GetElementStringValue(row, c, text.data(), static_cast<int>(text.size()));
                    a->SetElementStringValue(copy, c, const_cast<char *>(text.c_str()));
                    break;
                }
                case CKARRAYTYPE_PARAMETER: {
                    CKParameter *from = *reinterpret_cast<CKParameter **>(a->GetElement(row, c));
                    CKParameter *to = *reinterpret_cast<CKParameter **>(a->GetElement(copy, c));
                    if (from && to) to->CopyValue(from, FALSE);
                    break;
                }
                default: *a->GetElement(copy, c) = *a->GetElement(row, c); break;
                }
            }
        }
    });
}

EditResult MoveArrayRow(Document &document, CK_ID id, int row, int step) {
    CKDataArray *array = ArrayOf(document, id);
    const int to = row + step;
    if (!array || row < 0 || to < 0 || row >= array->GetRowCount() || to >= array->GetRowCount() || step == 0)
        return EditResult::Refused("The row cannot move further");
    return EditObjectState(document, id, step < 0 ? "Move Row Up" : "Move Row Down",
                           [row, to](CKObject *o) { CKDataArray::Cast(o)->SwapRows(row, to); });
}

EditResult SetArrayCells(Document &document, CK_ID id, const std::vector<int> &rows, int column, const std::string &text) {
    CKDataArray *array = ArrayOf(document, id);
    if (!ValidColumn(array, column) || rows.empty()) return EditResult::Refused("Select cells");
    bool refused = false;
    const EditResult result = EditObjectState(document, id, rows.size() > 1 ? "Edit Multiple" : "Edit Array",
                                              [&rows, column, &text, &refused](CKObject *o) {
                                                  CKDataArray *a = CKDataArray::Cast(o);
                                                  for (int row : rows)
                                                      if (row >= 0 && row < a->GetRowCount() &&
                                                          !a->SetElementStringValue(row, column, const_cast<char *>(text.c_str())))
                                                          refused = true;
                                              });
    if (refused && !result.ok) return EditResult::Refused("Value Already in the Array!");
    if (refused) {
        EditResult note = result;
        note.message = "Value Already in the Array!";
        return note;
    }
    return result;
}

EditResult PasteArrayParameters(Document &document, CK_ID id, const std::vector<int> &rows, int column,
                                const std::vector<CK_ID> &parameters) {
    CKContext *context = document.Context();
    CKDataArray *array = ArrayOf(document, id);
    if (!ValidColumn(array, column) || array->GetColumnType(column) != CKARRAYTYPE_PARAMETER)
        return EditResult::Refused("Not a parameter column");
    CKParameterManager *manager = context->GetParameterManager();
    const CKGUID type = array->GetColumnParameterGuid(column);
    std::vector<std::pair<int, CK_ID>> pasted;
    for (size_t i = 0; i < rows.size() && i < parameters.size(); ++i) {
        CKParameter *parameter = CKParameter::Cast(context->GetObject(parameters[i]));
        if (parameter && rows[i] >= 0 && rows[i] < array->GetRowCount() && manager->IsDerivedFrom(parameter->GetGUID(), type))
            pasted.emplace_back(rows[i], parameters[i]);
    }
    if (pasted.empty()) return EditResult::Refused("Nothing to paste!");
    return EditObjectState(document, id, "Paste", [context, column, pasted](CKObject *o) {
        for (const auto &entry : pasted)
            if (CKParameter *parameter = CKParameter::Cast(context->GetObject(entry.second)))
                CKDataArray::Cast(o)->SetElementValueFromParameter(entry.first, column, parameter);
    });
}

std::vector<char> MakeSaveFormat(CKContext *context, const std::string &extension) {
    CKPluginManager *plugins = CKGetPluginManager();
    if (!plugins || !context || extension.empty()) return {};
    CKFileExtension ext(const_cast<char *>(extension.c_str()));
    CKBitmapReader *reader = plugins->GetBitmapReader(ext);
    if (!reader) return {};
    std::vector<char> bytes;
    CKBitmapProperties *defaults = nullptr;
    reader->GetBitmapDefaultProperties(&defaults);
    if (defaults && defaults->m_Size >= static_cast<int>(sizeof(CKBitmapProperties))) {
        bytes.assign(reinterpret_cast<char *>(defaults), reinterpret_cast<char *>(defaults) + defaults->m_Size);
    } else {
        // Room for the reader's options after the header (CKCopyBitmapProperties
        // takes less than 256 bytes).
        bytes.assign(sizeof(CKBitmapProperties) + 128, 0);
        CKBitmapProperties *made = new (bytes.data()) CKBitmapProperties();
        made->m_Size = static_cast<int>(bytes.size());
        made->m_Ext = ext;
        CKGUID options(0, 0);
        for (int i = 0; i < plugins->GetPluginCount(CKPLUGIN_BITMAP_READER); ++i) {
            CKPluginEntry *entry = plugins->GetPluginInfo(CKPLUGIN_BITMAP_READER, i);
            if (!entry || _stricmp(entry->m_PluginInfo.m_Extension.m_Data, extension.c_str()) != 0) continue;
            made->m_ReaderGuid = entry->m_PluginInfo.m_GUID;
            if (entry->m_ReadersInfo) options = entry->m_ReadersInfo->m_SettingsParameterGuid;
            break;
        }
        if (options != CKGUID(0, 0))
            if (CKParameterOut *values = context->CreateCKParameterOut(nullptr, options, FALSE)) {
                plugins->SetReaderOptionData(context, bytes.data() + sizeof(CKBitmapProperties), values, ext, &made->m_ReaderGuid);
                context->DestroyObject(values);
            }
    }
    reader->Release();
    return bytes;
}

namespace {

// A parameter cell before and after a shortcut change.
struct ShortcutCell {
    int row = 0;
    CK_ID before = 0, after = 0;
};

level::DocumentFn WriteCells(CK_ID id, int column, std::vector<ShortcutCell> cells, bool after) {
    return [id, column, cells, after](Document &doc) {
        CKDataArray *array = CKDataArray::Cast(doc.Context()->GetObject(id));
        if (!array) return false;
        for (const ShortcutCell &cell : cells) {
            auto *element = array->GetElement(cell.row, column);
            if (!element) return false;
            *reinterpret_cast<CKParameter **>(element) = CKParameter::Cast(doc.Context()->GetObject(after ? cell.after : cell.before));
        }
        return true;
    };
}

} // namespace

EditResult PasteArrayShortcuts(Document &document, CK_ID id, const std::vector<int> &rows, int column,
                               const std::vector<CK_ID> &parameters) {
    CKContext *context = document.Context();
    CKDataArray *array = ArrayOf(document, id);
    if (!ValidColumn(array, column) || array->GetColumnType(column) != CKARRAYTYPE_PARAMETER)
        return EditResult::Refused("Not a parameter column");
    CKParameterManager *manager = context->GetParameterManager();
    const CKGUID type = array->GetColumnParameterGuid(column);
    std::vector<ShortcutCell> cells;
    std::set<CK_ID> buried;
    for (size_t i = 0; i < rows.size() && i < parameters.size(); ++i) {
        CKParameter *parameter = CKParameter::Cast(context->GetObject(parameters[i]));
        if (!parameter || parameter->GetOwner() == array || rows[i] < 0 || rows[i] >= array->GetRowCount() ||
            !manager->IsDerivedFrom(parameter->GetGUID(), type))
            continue;
        CKParameter *current = *reinterpret_cast<CKParameter **>(array->GetElement(rows[i], column));
        if (current == parameter) continue;
        cells.push_back({rows[i], current ? current->GetID() : 0, parameter->GetID()});
        if (current && current->GetOwner() == array) buried.insert(current->GetID());
    }
    if (cells.empty()) return EditResult::Refused("Nothing to paste!");
    return level::Apply(document, "Paste As Shortcut", WriteCells(id, column, cells, true), WriteCells(id, column, cells, false),
                        {}, 0, buried);
}

EditResult RemoveArrayShortcuts(Document &document, CK_ID id, const std::vector<int> &rows, int column) {
    CKContext *context = document.Context();
    CKDataArray *array = ArrayOf(document, id);
    if (!ValidColumn(array, column) || array->GetColumnType(column) != CKARRAYTYPE_PARAMETER)
        return EditResult::Refused("Not a parameter column");
    std::vector<ShortcutCell> cells;
    std::set<CK_ID> created;
    for (int row : rows) {
        if (row < 0 || row >= array->GetRowCount()) continue;
        CKParameter *current = *reinterpret_cast<CKParameter **>(array->GetElement(row, column));
        if (!current || current->GetOwner() == array) continue;
        CKParameter *copy = CKParameter::Cast(context->CopyObject(current));
        if (!copy) continue;
        copy->SetOwner(array);
        cells.push_back({row, current->GetID(), copy->GetID()});
        created.insert(copy->GetID());
    }
    if (cells.empty()) return EditResult::Refused("No shortcut");
    return level::Apply(document, "Remove Shortcut", WriteCells(id, column, cells, true), WriteCells(id, column, cells, false),
                        created);
}

EditResult AddGridLayer(Document &document, CK_ID grid, int type) {
    CKGrid *found = CKGrid::Cast(document.Context()->GetObject(grid));
    if (!found) return EditResult::Refused("Not a grid");
    if (GridGetLayer(found, type)) return EditResult::NoChange("The grid has a layer of this type");
    // The layer is a new object: it goes with the step (an undo discards it).
    Edit edit(document, "Add Layer");
    CK_ID layer = 0;
    const EditResult added = EditObjectState(document, grid, "Add Layer", [type, &layer](CKObject *o) {
        if (CKLayer *made = GridAddLayer(CKGrid::Cast(o), type)) layer = made->GetID();
    });
    if (!added.ok) return added;
    if (layer) edit.Created(layer);
    return edit.Commit(std::string(), layer);
}

EditResult RemoveGridLayer(Document &document, CK_ID grid, int type) {
    CKContext *context = document.Context();
    CKGrid *found = CKGrid::Cast(context->GetObject(grid));
    if (!found) return EditResult::Refused("Not a grid");
    CKLayer *layer = GridGetLayer(found, type);
    if (!layer) return EditResult::NoChange("The grid has no layer of this type");
    // CK destroys the layer: an undo makes one of the type and format again
    // and gives it the removed one's state (its squares, its visibility).
    const int format = layer->GetFormat();
    std::shared_ptr<CKStateChunk> state(CKSaveObjectState(layer), [](CKStateChunk *chunk) {
        if (chunk) DeleteCKStateChunk(chunk);
    });
    Edit edit(document, "Remove Layer");
    auto gridOf = [context, grid] { return CKGrid::Cast(context->GetObject(grid)); };
    if (!edit.Do([gridOf, type] { return gridOf() && GridRemoveLayer(gridOf(), type) == CK_OK; },
                 [gridOf, type, format, state] {
                     CKLayer *made = gridOf() ? GridAddLayer(gridOf(), type, format) : nullptr;
                     if (made && state) CKReadObjectState(made, state.get());
                     return made != nullptr;
                 }))
        return edit.Fail("The layer could not be removed");
    return edit.Commit();
}

EditResult AddGridClassification(Document &document, CK_ID grid, int classification) {
    if (!CKGrid::Cast(document.Context()->GetObject(grid))) return EditResult::Refused("Not a grid");
    return EditObjectState(document, grid, "Add Classification",
                           [classification](CKObject *o) { GridAddClassification(CKGrid::Cast(o), classification); });
}

EditResult RemoveGridClassification(Document &document, CK_ID grid, int classification) {
    if (!CKGrid::Cast(document.Context()->GetObject(grid))) return EditResult::Refused("Not a grid");
    return EditObjectState(document, grid, "Remove Classification",
                           [classification](CKObject *o) { GridRemoveClassification(CKGrid::Cast(o), classification); });
}

namespace {

// A grid's squares: its size, its place, each layer's type, format,
// visibility and values (row by row).
struct GridSquares {
    int width = 0, length = 0;
    VxMatrix world;
    VxVector scale;
    struct Layer {
        int type = 0, format = CKGRID_LAYER_FORMAT_NORMAL;
        bool visible = true;
        std::vector<int> values;
    };
    std::vector<Layer> layers;
};

GridSquares SquaresOf(CKGrid *grid) {
    GridSquares squares;
    squares.width = grid->GetWidth();
    squares.length = grid->GetLength();
    squares.world = grid->GetWorldMatrix();
    grid->GetScale(&squares.scale, TRUE);
    for (int i = 0; i < grid->GetLayerCount(); ++i) {
        CKLayer *layer = grid->GetLayerByIndex(i);
        if (!layer) continue;
        GridSquares::Layer kept;
        kept.type = layer->GetType();
        kept.format = layer->GetFormat();
        kept.visible = layer->IsVisible() != FALSE;
        kept.values.assign(static_cast<size_t>(squares.width) * squares.length, 0);
        for (int y = 0; y < squares.length; ++y)
            for (int x = 0; x < squares.width; ++x) layer->GetValue2(x, y, &kept.values[static_cast<size_t>(y) * squares.width + x]);
        squares.layers.push_back(std::move(kept));
    }
    return squares;
}

// The grid made as squares says: its layers again, its size, its place.
bool ApplySquares(CKGrid *grid, const GridSquares &squares) {
    if (!grid) return false;
    while (grid->GetLayerCount() > 0) {
        CKLayer *layer = grid->GetLayerByIndex(0);
        if (!layer || GridRemoveLayer(grid, layer->GetType()) != CK_OK) break;
    }
    // The squares keep their size: each is a unit of the grid's scale.
    grid->SetDimensions(squares.width, squares.length, squares.width * squares.scale.x, squares.length * squares.scale.z);
    grid->SetWorldMatrix(squares.world);
    for (const GridSquares::Layer &kept : squares.layers) {
        CKLayer *layer = GridAddLayer(grid, kept.type, kept.format);
        if (!layer) return false;
        layer->Show(kept.visible ? CKSHOW : CKHIDE);
        for (int y = 0; y < squares.length; ++y)
            for (int x = 0; x < squares.width; ++x) {
                int value = kept.values[static_cast<size_t>(y) * squares.width + x];
                layer->SetValue2(x, y, &value);
            }
    }
    return true;
}

CKGridManager *GridManagerOf(Document &document) {
    return static_cast<CKGridManager *>(document.Context()->GetManagerByGuid(GRID_MANAGER_GUID));
}

} // namespace

EditResult ResizeGrid(Document &document, CK_ID gridId, int side, bool add) {
    CKContext *context = document.Context();
    CKGrid *grid = CKGrid::Cast(context->GetObject(gridId));
    if (!grid) return EditResult::Refused("Not a grid");
    const GridSquares before = SquaresOf(grid);
    const bool columns = side == 0 || side == 1;
    if (!add && (columns ? before.width : before.length) <= 1) return EditResult::Refused("The grid keeps one column and one row");
    GridSquares after = before;
    const int dx = side == 0 ? (add ? 1 : -1) : 0, dy = side == 2 ? (add ? 1 : -1) : 0;
    if (columns) after.width += add ? 1 : -1;
    else after.length += add ? 1 : -1;
    for (GridSquares::Layer &layer : after.layers) {
        std::vector<int> values(static_cast<size_t>(after.width) * after.length, 0);
        for (int y = 0; y < before.length; ++y)
            for (int x = 0; x < before.width; ++x) {
                const int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < after.width && ny >= 0 && ny < after.length)
                    values[static_cast<size_t>(ny) * after.width + nx] = layer.values[static_cast<size_t>(y) * before.width + x];
            }
        layer.values = std::move(values);
    }
    // Left or bottom: the grid moves by the squares added or removed.
    if (dx || dy) {
        VxVector shift(-static_cast<float>(dx), 0.0f, -static_cast<float>(dy)), moved;
        Vx3DRotateVector(&moved, before.world, &shift); // the grid's axes, a square long
        after.world[3][0] += moved.x;
        after.world[3][1] += moved.y;
        after.world[3][2] += moved.z;
    }
    Edit edit(document, add ? (columns ? "Add Column" : "Add Row") : (columns ? "Delete Column" : "Delete Row"));
    if (!edit.Do([context, gridId, after] { return ApplySquares(CKGrid::Cast(context->GetObject(gridId)), after); },
                 [context, gridId, before] { return ApplySquares(CKGrid::Cast(context->GetObject(gridId)), before); }))
        return edit.Fail("The grid could not be changed");
    return edit.Commit();
}

EditResult RegisterGridLayerType(Document &document, const std::string &name) {
    CKGridManager *manager = GridManagerOf(document);
    if (!manager) return EditResult::Refused("No grid manager");
    std::string free = name.empty() ? "New Layer Type" : name;
    for (int n = 1; manager->GetTypeFromName(const_cast<CKSTRING>(free.c_str())) > 0; ++n)
        free = (name.empty() ? "New Layer Type" : name) + " " + std::to_string(n);
    const int type = manager->RegisterType(const_cast<CKSTRING>(free.c_str()));
    if (type <= 0) return EditResult::Refused("The layer type could not be registered");
    manager->SetAssociatedParam(type, CKPGUID_INT);
    document.MarkTopologyChanged();
    return EditResult::Applied(std::string(), static_cast<CK_ID>(type));
}

EditResult RenameGridLayerType(Document &document, int type, const std::string &name) {
    CKGridManager *manager = GridManagerOf(document);
    if (!manager || !manager->GetTypeName(type)) return EditResult::Refused("No such layer type");
    if (name.empty()) return EditResult::Refused("Give a name to the layer type");
    if (name == manager->GetTypeName(type)) return EditResult::NoChange("The name is unchanged");
    // Dev: a name in use is not taken, the old one stays.
    if (manager->GetTypeFromName(const_cast<CKSTRING>(name.c_str())) > 0) return EditResult::Refused("Name already used");
    if (manager->SetTypeName(type, const_cast<CKSTRING>(name.c_str())) != CK_OK) return EditResult::Refused("The layer type could not be renamed");
    document.MarkTopologyChanged();
    return EditResult::Applied();
}

EditResult SetGridLayerTypeColor(Document &document, int type, const VxColor &color) {
    CKGridManager *manager = GridManagerOf(document);
    if (!manager || !manager->GetTypeName(type)) return EditResult::Refused("No such layer type");
    VxColor now;
    manager->GetAssociatedColor(type, &now);
    if (now.r == color.r && now.g == color.g && now.b == color.b && now.a == color.a) return EditResult::NoChange("The color is unchanged");
    VxColor value = color;
    if (manager->SetAssociatedColor(type, &value) != CK_OK) return EditResult::Refused("The color could not be set");
    document.MarkTopologyChanged();
    return EditResult::Applied();
}

EditResult SetGridLayerTypeLinker(Document &document, int type, bool linker) {
    CKGridManager *manager = GridManagerOf(document);
    if (!manager || !manager->GetTypeName(type)) return EditResult::Refused("No such layer type");
    const CKGUID wanted = linker ? CKPGUID_LINKERGRAPH_ENUM : CKPGUID_INT;
    if (manager->GetAssociatedParam(type) == wanted) return EditResult::NoChange("The parameter type is unchanged");
    if (manager->SetAssociatedParam(type, wanted) != CK_OK) return EditResult::Refused("The parameter type could not be set");
    document.MarkTopologyChanged();
    return EditResult::Applied();
}

EditResult RegisterGridClassification(Document &document, const std::string &name) {
    CKGridManager *manager = GridManagerOf(document);
    if (!manager) return EditResult::Refused("No grid manager");
    std::string free = name.empty() ? "New Classification" : name;
    for (int n = 1; manager->GetClassificationFromName(const_cast<CKSTRING>(free.c_str())) >= 0; ++n)
        free = (name.empty() ? "New Classification" : name) + " " + std::to_string(n);
    const int classification = manager->RegisterClassification(const_cast<CKSTRING>(free.c_str()));
    if (classification < 0) return EditResult::Refused("The classification could not be registered");
    document.MarkTopologyChanged();
    return EditResult::Applied(std::string(), static_cast<CK_ID>(classification));
}

EditResult RenameGridClassification(Document &document, int classification, const std::string &name) {
    CKAttributeManager *attributes = document.Context()->GetAttributeManager();
    if (!attributes || !attributes->IsAttributeIndexValid(classification)) return EditResult::Refused("No such classification");
    if (name.empty()) return EditResult::Refused("Give a name to the classification");
    const char *current = attributes->GetAttributeNameByType(classification);
    if (current && name == current) return EditResult::NoChange("The name is unchanged");
    if (attributes->GetAttributeTypeByName(const_cast<CKSTRING>(name.c_str())) >= 0) return EditResult::Refused("Name already used");
    attributes->SetAttributeNameByType(classification, const_cast<CKSTRING>(name.c_str()));
    document.MarkTopologyChanged();
    return EditResult::Applied();
}

} // namespace vdev
