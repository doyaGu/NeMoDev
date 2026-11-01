#include "features/layout/SnapshotData.h"
#include <algorithm>
#include <cstring>
namespace vdev {
int SnapshotData::FirstFree() const {
    for (int slot = 0; slot < COUNT; ++slot)
        if (!Has(slot))
            return slot;
    return -1;
}
bool ReadSnapshot(const EditorClient &client, SnapshotDrag drag, std::vector<unsigned char> &pixels) {
    const auto result = client.Query("snapshot.pixels",
                                     {IntegerArgument("slot", drag.slot), IntegerArgument("revision", drag.revision)});
    const auto *field = EditorClient::Find(result, "pixels");
    if (result.status != EDITOR_OK || !field || field->type != EDITOR_INTEGER_LIST ||
        field->integers.size() != SnapshotData::WIDTH * SnapshotData::HEIGHT)
        return false;
    std::vector<unsigned char> next(field->integers.size() * 4);
    for (size_t i = 0; i < field->integers.size(); ++i) {
        if (field->integers[i] > UINT32_MAX)
            return false;
        const auto color = uint32_t(field->integers[i]);
        next[i * 4] = color & 255;
        next[i * 4 + 1] = (color >> 8) & 255;
        next[i * 4 + 2] = (color >> 16) & 255;
        next[i * 4 + 3] = 255;
    }
    pixels = std::move(next);
    return true;
}
bool ReadObjectPicture(const EditorClient &client, ObjectRef target, ObjectPictureData &data) {
    const auto version = client.Revision("object.picture");
    if (version && version == data.m_SourceVersion && target == data.target)
        return true;
    const auto result = client.Query("object.picture", {ObjectArgument("target", target)});
    const auto *echo = EditorClient::Find(result, "target"), *present = EditorClient::Find(result, "present"),
               *pixels = EditorClient::Find(result, "pixels");
    if (result.status != EDITOR_OK || !target || target.document != client.Scope().document || !echo ||
        echo->type != EDITOR_OBJECT || echo->object != target || !present || present->type != EDITOR_INTEGER_VALUE ||
        present->integer > 1 || !pixels || pixels->type != EDITOR_INTEGER_LIST ||
        pixels->integers.size() != (present->integer ? size_t(SnapshotData::WIDTH * SnapshotData::HEIGHT) : 0))
        return false;
    ObjectPictureData next;
    next.target = target;
    next.present = present->integer != 0;
    for (auto color : pixels->integers) {
        if (color > UINT32_MAX)
            return false;
        next.pixels.insert(next.pixels.end(),
                           {uint8_t(color), uint8_t(color >> 8), uint8_t(color >> 16), uint8_t(color >> 24)});
    }
    next.m_SourceVersion = client.Revision("object.picture");
    next.m_ImageRevision =
        data.m_ImageRevision + (data.target != target || data.present != next.present || data.pixels != next.pixels);
    data = std::move(next);
    return true;
}
bool ReadSnapshots(const EditorClient &client, SnapshotData &data) {
    const auto source = client.Revision("snapshot.slots");
    if (source && source == data.m_SourceVersion)
        return true;
    const auto result = client.Query("snapshot.slots");
    const auto *slots = EditorClient::Find(result, "slots");
    if (result.status != EDITOR_OK || !slots || slots->type != EDITOR_RECORD ||
        slots->fields.size() != SnapshotData::COUNT)
        return false;
    SnapshotData next = data;
    for (int i = 0; i < SnapshotData::COUNT; ++i) {
        const auto &row = slots->fields[i];
        if (row.key != std::to_string(i) || row.type != EDITOR_RECORD || row.fields.size() != 2 ||
            row.fields[0].key != "revision" || row.fields[0].type != EDITOR_INTEGER_VALUE ||
            row.fields[0].integer > UINT32_MAX || row.fields[1].key != "present" ||
            row.fields[1].type != EDITOR_INTEGER_VALUE || row.fields[1].integer > 1)
            return false;
        const auto revision = uint32_t(row.fields[0].integer);
        const bool present = row.fields[1].integer != 0;
        if (present && (data.slots[i].revision != revision || data.slots[i].pixels.empty()) &&
            !ReadSnapshot(client, {uint32_t(i), revision}, next.slots[i].pixels))
            return false;
        next.slots[i].revision = revision;
        next.slots[i].present = present;
        if (!present)
            next.slots[i].pixels.clear();
    }
    next.m_SourceVersion = client.Revision("snapshot.slots");
    data = std::move(next);
    return true;
}
EditorResponse RemoveSnapshots(const EditorClient &client, const SnapshotData &data, const std::vector<int> &slots) {
    EditorField entries{"slots", EDITOR_RECORD};
    for (auto slot : slots) {
        if (slot < 0 || slot >= SnapshotData::COUNT)
            return {EDITOR_INVALID};
        entries.fields.push_back({std::to_string(slot), EDITOR_INTEGER_VALUE, {}, {}, data.Revision(slot)});
    }
    const std::vector<EditorField> fields{std::move(entries)};
    const auto values = BorrowEditorFields(fields);
    return client.Command("snapshot.remove", {values.data(), values.data() + values.size()});
}
EditorResponse ApplySnapshot(const EditorClient &client, SnapshotDrag drag, ObjectRef target, bool script) {
    return client.Command("snapshot.apply",
                          {IntegerArgument("slot", drag.slot), IntegerArgument("revision", drag.revision),
                           ObjectArgument("target", target), IntegerArgument("script", script)});
}
} // namespace vdev
