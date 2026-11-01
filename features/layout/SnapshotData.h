#pragma once
#include "workspace/EditorClient.h"
#include <array>
#include <vector>
namespace vdev {
// Drag data names an image version; replacing a slot never changes a drag.
struct SnapshotDrag {
    uint32_t slot = 0, revision = 0;
};
constexpr const char *SNAPSHOT_PAYLOAD = "SCHEMATIC_SNAPSHOT";
struct SnapshotData {
    static constexpr int COUNT = 36, WIDTH = 52, HEIGHT = 40;
    struct Slot {
        uint32_t revision = 0;
        bool present = false;
        std::vector<unsigned char> pixels;
    };
    std::array<Slot, COUNT> slots;
    int FirstFree() const;
    bool Has(int slot) const { return slot >= 0 && slot < COUNT && slots[slot].present; }
    uint32_t Revision(int slot) const { return slot >= 0 && slot < COUNT ? slots[slot].revision : 0; }
    const std::vector<unsigned char> &Pixels(int slot) const { return slots.at(size_t(slot)).pixels; }

  private:
    uint64_t m_SourceVersion = 0;
    friend bool ReadSnapshots(const EditorClient &, SnapshotData &);
};
// The Setup preview is copied separately from the bitmap retained by document history.
struct ObjectPictureData {
    ObjectRef target{};
    bool present = false;
    std::vector<unsigned char> pixels;
    uint64_t Revision() const { return m_ImageRevision; }

private:
    uint64_t m_SourceVersion = 0, m_ImageRevision = 0;
    friend bool ReadObjectPicture(const EditorClient &, ObjectRef, ObjectPictureData &);
};
bool ReadObjectPicture(const EditorClient &, ObjectRef, ObjectPictureData &);
// Failed or inconsistent reads leave the previous copy unchanged.
bool ReadSnapshots(const EditorClient &, SnapshotData &);
bool ReadSnapshot(const EditorClient &, SnapshotDrag, std::vector<unsigned char> &);
EditorResponse RemoveSnapshots(const EditorClient &, const SnapshotData &, const std::vector<int> &);
EditorResponse ApplySnapshot(const EditorClient &, SnapshotDrag, ObjectRef target, bool script);
} // namespace vdev
