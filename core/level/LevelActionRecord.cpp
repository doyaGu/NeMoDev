#include "core/level/LevelActionRecord.h"

#include <type_traits>

#include "core/document/Document.h"
#include "core/document/Edit.h"
#include "core/runtime/CkRuntimeHost.h"

namespace vdev {
namespace {
ActionObject Reference(CKObject *object) { return object ? ActionObject{object->GetID(), object->GetClassID()} : ActionObject{}; }

CKObject *Resolve(Document &document, ActionObject reference) {
    auto *context = document.Context();
    auto *object = context && reference.id ? context->GetObject(reference.id) : nullptr;
    return object && object->GetClassID() == reference.cid ? object : nullptr;
}
bool EnvironmentReferences(Document &document, const SceneEnvironment &value) {
    return (!value.backgroundTexture || CKTexture::Cast(document.Context()->GetObject(value.backgroundTexture))) &&
           (!value.startingCamera || CKCamera::Cast(document.Context()->GetObject(value.startingCamera)));
}
bool ReadInteger(CKObject *object, ObjectIntegerProperty property, int &value) {
    switch (property) {
    case ObjectIntegerProperty::Visibility:
        value = object->IsHierarchicallyHide() ? CKHIERARCHICALHIDE : object->IsVisible() ? CKSHOW : CKHIDE;
        return true;
    case ObjectIntegerProperty::Priority:
        if (auto *behavior = CKBehavior::Cast(object)) { value = behavior->GetPriority(); return true; }
        if (auto *owner = CKBeObject::Cast(object)) { value = owner->GetPriority(); return true; }
        return false;
    case ObjectIntegerProperty::SaveOptions:
        value = SaveOptionOf(object);
        return value >= 0;
    case ObjectIntegerProperty::RenderPriority:
        if (auto *render = CKRenderObject::Cast(object)) { value = render->GetZOrder(); return true; }
        return false;
    }
    return false;
}
bool WriteInteger(CKObject *object, ObjectIntegerProperty property, int value) {
    int ignored;
    if (!ReadInteger(object, property, ignored)) return false;
    switch (property) {
    case ObjectIntegerProperty::Visibility: object->Show(static_cast<CK_OBJECT_SHOWOPTION>(value)); break;
    case ObjectIntegerProperty::Priority:
        if (auto *behavior = CKBehavior::Cast(object)) behavior->SetPriority(value);
        else CKBeObject::Cast(object)->SetPriority(value);
        break;
    case ObjectIntegerProperty::SaveOptions:
        if (auto *texture = CKTexture::Cast(object)) texture->SetSaveOptions(static_cast<CK_BITMAP_SAVEOPTIONS>(value));
        else if (auto *sprite = CKSprite::Cast(object)) sprite->SetSaveOptions(static_cast<CK_BITMAP_SAVEOPTIONS>(value));
        else CKSound::Cast(object)->SetSaveOptions(static_cast<CK_SOUND_SAVEOPTIONS>(value));
        break;
    case ObjectIntegerProperty::RenderPriority: CKRenderObject::Cast(object)->SetZOrder(value); break;
    }
    return true;
}

template <typename T> bool ReadCurrent(Document &document, const T &change, T &entry) {
    entry = change;
    auto *object = Resolve(document, entry.object);
    if (!object) return false;
    if constexpr (std::is_same_v<T, ObjectNameChange>) {
        entry.before = entry.after = object->GetName() ? object->GetName() : "";
    } else if constexpr (std::is_same_v<T, ObjectIntegerChange>) {
        int value;
        if (!ReadInteger(object, entry.property, value)) return false;
        entry.before = entry.after = value;
    } else if constexpr (std::is_same_v<T, ObjectParentChange>) {
        auto parentValid = [&](ActionObject parent, CK_CLASSID cid) {
            auto *resolved = Resolve(document, parent);
            return !parent.id || (resolved && CKIsChildClassOf(resolved, cid));
        };
        ParentState state;
        if (auto *entity = CK3dEntity::Cast(object)) {
            if (!parentValid(entry.before.parent, CKCID_3DENTITY) || !parentValid(entry.after.parent, CKCID_3DENTITY)) return false;
            state.parent = Reference(entity->GetParent());
        } else if (auto *entity = CK2dEntity::Cast(object)) {
            if (!parentValid(entry.before.parent, CKCID_2DENTITY) || !parentValid(entry.after.parent, CKCID_2DENTITY)) return false;
            state.parent = Reference(entity->GetParent());
            state.background = entity->IsBackground() != FALSE;
        } else return false;
        entry.before = entry.after = state;
    } else if constexpr (std::is_same_v<T, SceneEnvironmentChange>) {
        auto *scene = CKScene::Cast(object);
        if (!scene || !EnvironmentReferences(document, entry.before) || !EnvironmentReferences(document, entry.after)) return false;
        entry.before = entry.after = ReadSceneEnvironment(scene);
    }
    return true;
}

template <typename T> bool Write(Document &document, std::uint64_t generation, const T &change, bool undo) {
    bool written = false;
    return GuardedCall([&] {
        if (document.Host().Generation() != generation) return;
        const auto &entry = change;
        auto write = [&] {
            auto *object = Resolve(document, entry.object);
            if (!object) return false;
            const auto &value = undo ? entry.before : entry.after;
            if constexpr (std::is_same_v<T, ObjectNameChange>) object->SetName(const_cast<CKSTRING>(value.c_str()));
            else if constexpr (std::is_same_v<T, ObjectIntegerChange>) return WriteInteger(object, entry.property, value);
            else if constexpr (std::is_same_v<T, ObjectParentChange>) {
                auto *parent = value.parent.id ? Resolve(document, value.parent) : nullptr;
                if (value.parent.id && !parent) return false;
                if (auto *entity = CK3dEntity::Cast(object)) {
                    for (auto *up = CK3dEntity::Cast(parent); up; up = up->GetParent()) if (up == entity) return false;
                    return entity->SetParent(CK3dEntity::Cast(parent), TRUE) != FALSE;
                }
                if (auto *entity = CK2dEntity::Cast(object)) {
                    for (auto *up = CK2dEntity::Cast(parent); up; up = up->GetParent()) if (up == entity) return false;
                    if (!entity->SetParent(CK2dEntity::Cast(parent))) return false;
                    entity->SetBackground(value.background);
                } else return false;
            } else if constexpr (std::is_same_v<T, SceneEnvironmentChange>) {
                auto *scene = CKScene::Cast(object);
                if (!scene || !EnvironmentReferences(document, value)) return false;
                ApplySceneEnvironment(document.Context(), scene, value);
            }
            return true;
        };
        written = write();
        if (!written || document.Host().Generation() != generation) { written = false; return; }
        T actual;
        written = ReadCurrent(document, change, actual) && actual.after == (undo ? change.before : change.after);
    }, &document.Host(), "Replay level action record") && written;
}

class RecordedLevelAction final : public Action {
public:
    explicit RecordedLevelAction(LevelActionRecord record) : m_Record(std::move(record)) {}
    bool Redo(Document &document) override { return Apply(document, false); }
    bool Undo(Document &document) override { return Apply(document, true); }
    void RetainObjects(std::set<CK_ID> &objects) const override {
        if (m_Record.changes.valueless_by_exception()) return;
        const auto keep = [&](CK_ID id) { if (id) objects.insert(id); };
        std::visit([&](const auto &changes) {
            for (const auto &entry : changes) {
                keep(entry.object.id);
                using T = std::decay_t<decltype(entry)>;
                if constexpr (std::is_same_v<T, ObjectParentChange>) {
                    keep(entry.before.parent.id);
                    keep(entry.after.parent.id);
                } else if constexpr (std::is_same_v<T, SceneEnvironmentChange>) {
                    keep(entry.before.backgroundTexture);
                    keep(entry.before.startingCamera);
                    keep(entry.after.backgroundTexture);
                    keep(entry.after.startingCamera);
                }
            }
        }, m_Record.changes);
    }
private:
    bool Apply(Document &document, bool undo) {
        if (m_Record.documentInstance != document.InstanceId() || m_Record.changes.valueless_by_exception()) return false;
        return std::visit([&](const auto &changes) { return Apply(document, changes, undo); }, m_Record.changes);
    }
    template <typename T> bool Apply(Document &document, const std::vector<T> &changes, bool undo) {
        if (m_Record.contextGeneration != document.Host().Generation() || changes.empty()) return false;
        std::vector<T> previous;
        const bool captured = GuardedCall([&] {
            previous.reserve(changes.size());
            for (const auto &change : changes) {
                T state;
                if (!ReadCurrent(document, change, state)) return;
                previous.push_back(std::move(state));
            }
        }, &document.Host(), "Validate level action record");
        if (!captured || previous.size() != changes.size()) return false;
        const auto index = [&](size_t step) { return undo ? previous.size() - 1 - step : step; };
        for (size_t step = 0; step < previous.size(); ++step) {
            if (Write(document, m_Record.contextGeneration, changes[index(step)], undo)) continue;
            bool restored = true;
            for (size_t rollback = step + 1; rollback-- > 0;)
                restored = Write(document, m_Record.contextGeneration, previous[index(rollback)], false) && restored;
            if (!restored) {
                document.Host().Log(LogLevel::Error, "Level action record recovery was incomplete: the undo history is cleared");
                if (document.Host().Generation() == m_Record.contextGeneration) document.InvalidateHistory();
            }
            return false;
        }
        return true;
    }
    LevelActionRecord m_Record;
};
} // namespace

std::unique_ptr<Action> RestoreLevelAction(LevelActionRecord record) { return std::make_unique<RecordedLevelAction>(std::move(record)); }

EditResult ApplyLevelActionRecord(Document &document, const std::string &label, LevelActionRecord record) {
    Edit edit(document, label);
    if (!edit.Do(RestoreLevelAction(std::move(record)))) {
        edit.Fail(label + " failed");
        return EditResult::Error(label + " failed; check the runtime log for recovery failures");
    }
    return edit.Commit();
}
} // namespace vdev
