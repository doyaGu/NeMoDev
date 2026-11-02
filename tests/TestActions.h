#pragma once

#include "core/document/Edit.h"

namespace vdevtest {
// Faults and scalar probes belong to the tests. Production history accepts
// only domain actions with owned data and lifetime-qualified references.
class CallbackAction final : public vdev::Action {
public:
    CallbackAction(std::function<bool()> redo, std::function<bool()> undo)
        : m_Redo(std::move(redo)), m_Undo(std::move(undo)) {}
    bool Redo(vdev::Document &) override { return m_Redo(); }
    bool Undo(vdev::Document &) override { return m_Undo(); }

private:
    std::function<bool()> m_Redo, m_Undo;
};
inline std::unique_ptr<vdev::Action> TestAction(std::function<bool()> redo, std::function<bool()> undo) {
    return std::make_unique<CallbackAction>(std::move(redo), std::move(undo));
}
} // namespace vdevtest
