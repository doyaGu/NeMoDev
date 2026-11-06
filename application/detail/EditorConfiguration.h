#pragma once

#include "application/detail/EditorModules.h"

namespace vdev::detail {
class SelectedConfiguration {
  public:
    SelectedConfiguration();
    // A selection is prepared in isolation. Readers only see the installed one;
    // its descriptor pointers remain owned by RegisteredModules until shutdown.
    struct Selection {
        std::map<std::string, const EditorCommandDesc *> commands;
        std::map<std::string, const EditorQueryDesc *, std::less<>> queries;
        EditorValueEditors editors;
        std::map<std::string, const EditorViewDesc *> views;
        std::map<std::string, const EditorToolDesc *> tools;
        std::map<std::string, const EditorThemeDesc *> themes;
        std::map<std::string, const EditorFontDesc *> fonts;
        std::map<std::string, const EditorImageDesc *> images;
        std::map<std::string, const EditorDocumentDataDesc *> documentData;
        std::shared_ptr<const DocumentDataRegistry> dataTypes;
        std::map<std::string, std::map<std::string, EditorField>> themeValues;
        uint64_t appearanceRevision = 1;
        std::map<std::string, EditorToolbarLocation> toolbars;
        std::vector<ToolbarEntry> toolbarItems;
        std::map<std::string, std::string> bindingViews;
        std::map<std::string, EditorInputPhase> bindingPhases;
        std::map<std::string, PropertyBinding> properties;
        std::vector<MenuEntry> menus;
        std::vector<std::pair<std::string, std::string>> bindings;
        EditorConfiguration configuration;
        std::map<std::string, std::vector<EditorKey>> shortcuts;
        std::map<std::string, std::string> locations;
    };
    const Selection &Get() const { return m_Current; }
    Selection Prepare(const std::vector<Module *> &modules, const EditorConfiguration &configuration) const;
    bool EditingProvidersUnchanged(const Selection &next) const {
        return m_Current.commands == next.commands && m_Current.queries == next.queries &&
               m_Current.editors == next.editors && m_Current.views == next.views && m_Current.tools == next.tools &&
               m_Current.documentData == next.documentData;
    }
    void Install(Selection &&next) noexcept { m_Current = std::move(next); }
    void Clear();

  private:
    Selection m_Current;
};
} // namespace vdev::detail
