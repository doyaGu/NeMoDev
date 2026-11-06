#include "ui/layout/LayoutWindows.h"
#include "core/layout/LayoutView.h"
#include "core/layout/LayoutInterface.h"
#include "core/runtime/CkRuntimeHost.h"
#include "ui/schematic/Snapshots.h"
#include "features/layout/LayoutSceneData.h"
#include "workspace/EditorClient.h"
#include <stdexcept>

namespace vdev {
EditorStatus LayoutWindows::DocumentChange(const EditorDocumentChange &change) {
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
