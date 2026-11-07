#include "ui/layout/LayoutWindows.h"
#include "core/layout/LayoutView.h"
#include "core/layout/LayoutInterface.h"
#include "core/runtime/CkRuntimeHost.h"
#include "ui/schematic/Snapshots.h"
#include "features/layout/LayoutSceneData.h"
#include "workspace/EditorClient.h"
#include "application/FileLoadState.h"
#include <stdexcept>

namespace vdev {
struct LayoutWindows::LoadedLayout {
    DocumentToken document{};
    LayoutInterfaceData data;
    LayoutRestoreData restore;
};
LayoutWindows::~LayoutWindows() { DiscardLoadedFile(); }
void LayoutWindows::DiscardLoadedFile() { delete m_LoadedLayout; m_LoadedLayout = nullptr; }

EditorStatus LayoutWindows::DocumentChange(const EditorDocumentChange &change) {
    if (change.phase == EDITOR_DOCUMENT_LOADED) return CaptureLoadedFile() ? EDITOR_OK : EDITOR_ERROR;
    if (change.phase == EDITOR_DOCUMENT_DETACH) {
        ForEach([&](LayoutTarget target) {
            auto &window = *static_cast<LayoutWindow *>(target.viewport);
            if (!window.m_Window) return;
            window.Window().capturing = 0;
            window.ReleaseInputViewport();
            const bool released = window.ReleaseTexture();
            window.m_View->Detach();
            if (!released) window.ResetViewportAfterContextStop();
            window.m_Started = false;
        });
        m_InputOwner = nullptr;
        return EDITOR_OK;
    }
    if (change.phase == EDITOR_DOCUMENT_REPLACED) {
        DiscardLoadedFile();
        DocumentClosed();
        ForEach([](LayoutTarget target) {
            static_cast<LayoutWindow *>(target.viewport)->ApplyLayoutData(LayoutInterfaceData{}, {});
        });
        ReleaseSnapshotTextures();
    }
    std::string error;
    if (!Start(m_MainWindow, &error)) throw std::runtime_error("3D Layout: " + error);
    return EDITOR_OK;
}

bool LayoutWindows::CaptureLoadedFile() {
    FileLoadState loaded;
    if (!ReadFileLoadState(m_Editor.Host(), loaded)) return false;
    if (!loaded.opened || !loaded.composition) return true;
    const auto views = m_Editor.Views();
    const auto selected = std::find_if(views.begin(), views.end(), [](const auto &view) {
        return std::string(view.id.data, view.id.size) == "layout.viewport";
    });
    if (selected == views.end() || selected->data != this) return true;
    const auto result = EditorClient(m_Editor.Host(), 0).Query("layout.file-interface");
    if (result.status != EDITOR_OK) return false;
    const auto *group = EditorClient::Find(result, "group"), *camera = EditorClient::Find(result, "camera"),
               *objects = EditorClient::Find(result, "objects"), *setups = EditorClient::Find(result, "setups"),
               *locked = EditorClient::Find(result, "locked");
    if (!group || group->type != EDITOR_OBJECT || !camera || camera->type != EDITOR_OBJECT ||
        !objects || objects->type != EDITOR_OBJECT_LIST || !setups || setups->type != EDITOR_OBJECT_LIST ||
        !locked || locked->type != EDITOR_INTEGER_VALUE || locked->integer > 1) return false;
    const auto current = [this](ObjectRef ref) {
        return !ref || (ref.document == m_Editor.Scope().document && m_Runtime.Context()->GetObjectByHandle({ref.object, ref.lifetime}));
    };
    if (!current(group->object) || !current(camera->object) ||
        !std::all_of(objects->objects.begin(), objects->objects.end(), current) ||
        !std::all_of(setups->objects.begin(), setups->objects.end(), current)) return false;
    LayoutRestoreData restore;
    restore.group = group->object; restore.camera = camera->object;
    restore.selection = objects->objects; restore.setups = setups->objects; restore.locked = locked->integer != 0;
    auto next = std::make_unique<LoadedLayout>();
    next->data = ReadLayoutInterfaceData(m_Runtime.Context());
    next->restore = std::move(restore); next->document = m_Editor.Scope().document;
    delete m_LoadedLayout; m_LoadedLayout = next.release();
    return true;
}
void LayoutWindows::RestoreLoadedFile() {
    if (!m_LoadedLayout || !m_Editor.NativeAvailable()) return;
    std::unique_ptr<LoadedLayout> loaded(m_LoadedLayout); m_LoadedLayout = nullptr;
    if (loaded->document == m_Editor.Scope().document) {
        const auto views = m_Editor.Views();
        if (std::any_of(views.begin(), views.end(), [this](const auto &view) {
                return std::string(view.id.data, view.id.size) == "layout.viewport" && view.data == this;
            })) ApplyLayoutData(loaded->data, loaded->restore);
    }
}

LayoutInterfaceData LayoutWindows::LayoutData() const {
    auto data = Primary().LayoutData();
    auto *context = m_Runtime.Context();
    if (!context) return data;
    data.setups.clear();
    const EditorClient client(m_Editor.Host(), 0);
    const auto views = client.Query("workspace.instances");
    const auto *ids = EditorClient::Find(views, "views"), *types = EditorClient::Find(views, "types");
    if (views.status != EDITOR_OK || !ids || !types || ids->integers.size() != types->texts.size()) return data;
    const auto front = m_Workspace.LastFocused("object.setup");
    int order = 2;
    for (size_t i = 0; i < ids->integers.size(); ++i) {
        if (types->texts[i] != "object.setup") continue;
        const auto state = client.Inspect(ids->integers[i]);
        const auto *target = EditorClient::Find(state, "target"), *other = EditorClient::Find(state, "other");
        if (state.status != EDITOR_OK || !target || !target->object) continue;
        // View inspection supplies complete identities, never unverified SDK ids.
        if (target->object.document != m_Editor.Scope().document) continue;
        auto *object = context->GetObjectByHandle({target->object.object, target->object.lifetime});
        LayoutInterfaceData::Setup setup;
        if (!object || !LayoutSetupGuid(object, setup.guid)) continue;
        setup.object = object->GetID();
        if (other && other->object.document == target->object.document &&
            context->GetObjectByHandle({other->object.object, other->object.lifetime})) setup.other = other->object.object;
        setup.values[0] = 2;
        setup.values[1] = order++;
        setup.values[2] = ids->integers[i] == front;
        data.setups.push_back(setup);
    }
    return data;
}

void LayoutWindows::ApplyLayoutData(const LayoutInterfaceData &data, const LayoutRestoreData &restore) {
    Primary().ApplyLayoutData(data, restore);
    for (const auto ref : restore.setups)
        EditorClient(m_Editor.Host(), 0).Command("object.open-setup", {ObjectArgument("target", ref)});
}
} // namespace vdev
