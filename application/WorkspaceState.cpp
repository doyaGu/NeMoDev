#include "application/WorkspaceState.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace vdev {
bool ValidateWorkspaceState(const WorkspaceState &state, std::string &error) {
    const auto invalid = [&] {
        error = "Invalid saved workspace";
        return false;
    };
    if (state.views.size() > 1024 || state.layout.size() > 1024 * 1024 || state.layout.find('\0') != std::string::npos)
        return invalid();
    std::set<uint64_t> instances;
    std::set<std::string> roles;
    std::function<bool(const std::vector<EditorField> &)> values = [&](const auto &fields) {
        for (const auto &field : fields) {
            if (field.type == EDITOR_OBJECT || field.type == EDITOR_OBJECT_LIST || !values(field.fields))
                return false;
        }
        return true;
    };
    for (const auto &view : state.views) {
        if (!view.instance || view.instance == UINT64_MAX || !instances.insert(view.instance).second ||
            view.type.empty() || view.tool.size() > 1024 || view.tool.find('\0') != std::string::npos ||
            (view.open && !view.role.empty() && !roles.insert(view.role).second) ||
            (!view.version && !view.settings.empty()) || !values(view.settings))
            return invalid();
        if (view.placed && (!std::isfinite(view.x) || !std::isfinite(view.y) || !std::isfinite(view.width) ||
                            !std::isfinite(view.height) || std::abs(view.x) > 1000000 || std::abs(view.y) > 1000000 ||
                            view.width < 1 || view.height < 1 || view.width > 100000 || view.height > 100000))
            return invalid();
        try {
            const auto borrowed = BorrowEditorFields(view.settings);
            std::vector<EditorField> checked;
            if (CopyEditorFields(checked, borrowed.data(), uint32_t(borrowed.size())) != EDITOR_OK)
                return invalid();
        } catch (...) {
            return invalid();
        }
    }
    if (state.focused && std::none_of(state.views.begin(), state.views.end(),
                                      [&](const auto &view) { return view.instance == state.focused && view.open; }))
        return invalid();
    error.clear();
    return true;
}
} // namespace vdev
