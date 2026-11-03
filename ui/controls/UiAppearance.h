#pragma once

#include "application/EditorApplication.h"
#include "ui/render/HtmlRenderer.h"
#include <memory>

namespace vdev {
// The classic UI is an ordinary module and contributes its own default choices.
bool RegisterDefaultAppearance(EditorApplication &, std::string &error);
EditorConfiguration DefaultAppearanceConfiguration();

// Owns prepared UI resources. Configuration is checked before publication;
// native style/font changes are applied between submitted frames.
class UiAppearance {
public:
    UiAppearance();
    ~UiAppearance();
    UiAppearance(const UiAppearance &) = delete;
    UiAppearance &operator=(const UiAppearance &) = delete;
    bool Prepare(const EditorApplication &, std::string &error);
    bool Apply(const EditorApplication &, float contentScale, std::string &error);
    void Reset();
    HtmlFonts Fonts() const;
    uint64_t Revision() const;

private:
    struct State;
    std::unique_ptr<State> m_State;
};
} // namespace vdev
