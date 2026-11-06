#include "application/detail/EditorConfiguration.h"
#include "application/detail/EditorValidation.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace vdev::detail {
SelectedConfiguration::Selection SelectedConfiguration::Prepare(const std::vector<Module *> &modules,
                                                                const EditorConfiguration &configuration) const {
    Selection selected;
    for (const auto *module : modules) {
        for (const auto &location : module->locations)
            selected.locations.emplace(String(location.id), String(location.title));
        for (const auto &d : module->properties)
            selected.properties.emplace(String(d.id),
                                        PropertyBinding{String(d.id), String(d.label), String(d.query), String(d.field),
                                                        String(d.command), String(d.argument), String(d.editor),
                                                        String(d.choices)});
    }
    decltype(selected.commands) commands;
    decltype(selected.queries) queries;
    decltype(selected.editors) editors;
    decltype(selected.views) views;
    decltype(selected.themes) themes;
    decltype(selected.fonts) fonts;
    decltype(selected.images) images;
    decltype(selected.documentData) documentData;
    decltype(selected.themeValues) themeValues;
    std::map<std::string, const EditorToolbarItemDesc *> toolbarDescriptors;
    auto choose = [&](auto member, auto &table, const char *kind) {
        for (auto *m : modules)
            for (auto &d : m->*member) {
                const auto id = String(d.id);
                auto choice = std::find_if(configuration.providers.begin(), configuration.providers.end(),
                                           [&](const ProviderChoice &c) { return c.kind == kind && c.id == id; });
                if (choice == configuration.providers.end()) {
                    if (table.count(id)) throw std::invalid_argument("Ambiguous provider: " + id);
                    table[id] = &d;
                } else if (choice->provider == String(d.provider))
                    table[id] = &d;
            }
    };
    choose(&Module::commands, commands, "command");
    choose(&Module::queries, queries, "query");
    choose(&Module::editors, editors, "editor");
    choose(&Module::views, views, "view");
    choose(&Module::toolbarItems, toolbarDescriptors, "toolbar");
    choose(&Module::themes, themes, "theme");
    choose(&Module::fonts, fonts, "font");
    choose(&Module::images, images, "image");
    choose(&Module::documentData, documentData, "document-data");
    std::set<std::pair<std::string, std::string>> choices;
    for (auto &c : configuration.providers) {
        if (!choices.emplace(c.kind, c.id).second) throw std::invalid_argument("Duplicate provider selection");
        const bool found = c.kind == "command"         ? commands.count(c.id)
                           : c.kind == "query"         ? queries.count(c.id)
                           : c.kind == "editor"        ? editors.count(c.id)
                           : c.kind == "view"          ? views.count(c.id)
                           : c.kind == "theme"         ? themes.count(c.id)
                           : c.kind == "font"          ? fonts.count(c.id)
                           : c.kind == "document-data" ? documentData.count(c.id)
                           : c.kind == "image"         ? images.count(c.id)
                           : c.kind == "toolbar"       ? toolbarDescriptors.count(c.id)
                                                       : false;
        if (!found) throw std::invalid_argument("Unknown provider selection");
    }
    std::set<const EditorThemeDesc *> visiting;
    const auto compose = [&](const auto &self, const EditorThemeDesc &theme,
                             std::map<std::string, EditorField> &values) -> void {
        if (visiting.size() >= 64 || !visiting.insert(&theme).second)
            throw std::invalid_argument("Theme inheritance cycle: " + String(theme.id));
        if (theme.parent.size) {
            const EditorThemeDesc *parent = nullptr;
            for (const auto *module : modules)
                for (const auto &d : module->themes)
                    if (String(d.id) == String(theme.id) && String(d.provider) == String(theme.parent)) parent = &d;
            if (!parent) throw std::invalid_argument("Missing parent theme provider: " + String(theme.parent));
            self(self, *parent, values);
        }
        EditorValue record{};
        record.key = EDITOR_TEXT("theme");
        record.type = EDITOR_RECORD;
        record.fields = {theme.values, theme.count};
        std::vector<EditorField> copied;
        if (CopyEditorFields(copied, &record, 1) != EDITOR_OK) throw std::invalid_argument("Invalid theme roles");
        for (auto &field : copied.front().fields) {
            const auto old = values.find(field.key);
            if (old != values.end() && old->second.type != field.type)
                throw std::invalid_argument("Incompatible inherited theme role: " + field.key);
            values[field.key] = std::move(field);
        }
        visiting.erase(&theme);
    };
    for (const auto &theme : themes) {
        auto &values = themeValues[theme.first];
        compose(compose, *theme.second, values);
        for (const auto &field : values) {
            if (field.first.compare(0, 5, "font.") == 0 && !fonts.count(field.second.text))
                throw std::invalid_argument("Unknown theme font: " + field.second.text);
            if (field.first.compare(0, 6, "image.") == 0 && !images.count(field.second.text))
                throw std::invalid_argument("Unknown theme image: " + field.second.text);
        }
    }
    auto properties = selected.properties;
    for (auto &p : properties) {
        auto &d = p.second;
        if (!commands.count(d.command) || !queries.count(d.query) || !editors.count(d.editor))
            throw std::invalid_argument("Unresolved property contribution");
        if (!d.choices.empty() && !queries.count(d.choices))
            throw std::invalid_argument("Unknown property choices query");
        auto *c = commands.at(d.command);
        auto *q = queries.at(d.query);
        if (c->count < 2 || q->count < 1 || c->effect != EDITOR_DOCUMENT_EDIT)
            throw std::invalid_argument("Property command/query schema mismatch");
        const auto has = [](auto *p, uint32_t count, const std::string &key, EditorValueType type) {
            for (uint32_t i = 0; i < count; ++i)
                if (String(p[i].key) == key && p[i].type == type) return true;
            return false;
        };
        d.type = editors.at(d.editor)->type;
        d.commandProvider = String(c->provider);
        d.queryProvider = String(q->provider);
        if (!d.choices.empty()) {
            const auto *choices = queries.at(d.choices);
            d.choicesProvider = String(choices->provider);
            // A record editor defines its own choice projection. Scalar
            // editors keep the paired label/value lists of their value type.
            if (choices->area != EDITOR_DOCUMENT ||
                !has(choices->parameters, choices->count, "target", EDITOR_OBJECT) ||
                (d.type != EDITOR_RECORD &&
                 (!has(choices->fields, choices->fieldCount, "labels", EDITOR_TEXT_LIST) ||
                  !has(choices->fields, choices->fieldCount,
                       (d.type == EDITOR_OBJECT || d.type == EDITOR_OBJECT_LIST) ? "objects" : "values",
                       (d.type == EDITOR_OBJECT || d.type == EDITOR_OBJECT_LIST) ? EDITOR_OBJECT_LIST
                       : d.type == EDITOR_INTEGER_VALUE                          ? EDITOR_INTEGER_LIST
                                                                                 : EDITOR_TEXT_LIST))))
                throw std::invalid_argument("Property choices query schema mismatch");
            for (uint32_t i = 0; i < choices->count; ++i)
                if (String(choices->parameters[i].key) != "target" && !choices->parameters[i].optional)
                    throw std::invalid_argument("Property choices query requires an unbound argument");
        }
        if (!has(q->fields, q->fieldCount, d.field, d.type))
            throw std::invalid_argument("Unknown property value field");
        if (!has(c->parameters, c->count, "target", EDITOR_OBJECT) ||
            !has(c->parameters, c->count, d.argument, d.type) || !has(q->parameters, q->count, "target", EDITOR_OBJECT))
            throw std::invalid_argument("Property command/query schema mismatch");
        for (uint32_t i = 0; i < c->count; ++i) {
            const auto key = String(c->parameters[i].key);
            if (key != "target" && key != d.argument && !has(q->fields, q->fieldCount, key, c->parameters[i].type))
                throw std::invalid_argument("Property command context field is missing");
        }
    }
    const auto actionExists = [&](const std::string &id) {
        const bool property = selected.properties.count(id), command = commands.count(id);
        if (property == command) return false;
        if (command)
            for (uint32_t i = 0; i < commands.at(id)->count; ++i)
                if (!commands.at(id)->parameters[i].optional) return false;
        return true;
    };
    std::vector<MenuEntry> menus;
    std::vector<std::pair<std::string, std::string>> bindings;
    decltype(selected.tools) tools;
    decltype(selected.toolbars) toolbars;
    decltype(selected.bindingViews) bindingViews;
    decltype(selected.bindingPhases) bindingPhases;
    for (const auto &entry : commands)
        if (entry.second->view.size) bindingViews[entry.first] = String(entry.second->view);
    for (auto *m : modules) {
        for (auto &d : m->tools)
            if (auto chosen = commands.find(String(d.id));
                chosen != commands.end() && String(chosen->second->provider) == String(d.provider)) {
                if (!views.count(String(d.view))) throw std::invalid_argument("Unknown tool view");
                tools.emplace(String(d.id), &d);
                bindingViews[String(d.id)] = String(d.view);
            }
        for (auto &d : m->toolbars) {
            if (d.view.size && !views.count(String(d.view))) throw std::invalid_argument("Unknown toolbar view");
            toolbars.emplace(String(d.id), d);
        }
    }
    for (const auto &entry : views)
        if (entry.second->defaultTool.size &&
            (!tools.count(String(entry.second->defaultTool)) ||
             String(tools.at(String(entry.second->defaultTool))->view) != entry.first))
            throw std::invalid_argument("Unknown default view tool");
    for (auto *m : modules) {
        for (auto &d : m->menus) {
            if (!actionExists(String(d.action)) || !selected.locations.count(String(d.location)))
                throw std::invalid_argument("Unknown menu action or location");
            if (d.checked.size) {
                const auto query = queries.find(String(d.checked));
                if (query == queries.end() || query->second->count || query->second->fieldCount != 1 ||
                    String(query->second->fields[0].key) != "checked" ||
                    query->second->fields[0].type != EDITOR_INTEGER_VALUE)
                    throw std::invalid_argument(
                        "Menu check query must return an integer checked field without arguments");
            }
            menus.push_back(
                {String(d.id), String(d.action), String(d.location), String(d.label), d.order, String(d.checked)});
        }
        for (auto &d : m->bindings) {
            if (!actionExists(String(d.action))) throw std::invalid_argument("Unknown binding action");
            const auto view = d.view.size ? String(d.view)
                                          : (bindingViews.count(String(d.action)) ? bindingViews.at(String(d.action))
                                                                                  : std::string{});
            if ((!view.empty() && !views.count(view)) ||
                (bindingViews.count(String(d.action)) && bindingViews.at(String(d.action)) != view))
                throw std::invalid_argument("Inconsistent binding view");
            bindingViews[String(d.action)] = view;
            if (bindingPhases.count(String(d.action)) && bindingPhases.at(String(d.action)) != d.phase)
                throw std::invalid_argument("Conflicting input phases");
            bindingPhases[String(d.action)] = d.phase;
            const auto binding = std::make_pair(String(d.action), String(d.chord));
            if (std::find(bindings.begin(), bindings.end(), binding) == bindings.end()) bindings.push_back(binding);
        }
    }
    std::map<std::string, ToolbarEntry> toolbarEntries;
    for (const auto &entry : toolbarDescriptors) {
        const auto &d = *entry.second;
        ToolbarEntry item{String(d.id),    String(d.provider), String(d.location),
                          String(d.title), String(d.action),   String(d.checked)};
        if (CopyEditorFields(item.arguments, d.arguments, d.count) != EDITOR_OK)
            throw std::invalid_argument("Invalid toolbar arguments");
        item.x = d.x;
        item.y = d.y;
        item.width = d.width;
        item.height = d.height;
        item.flags = d.flags;
        item.draw = d.draw;
        item.data = d.data;
        toolbarEntries.emplace(entry.first, std::move(item));
    }
    std::set<std::string> toolbarOverrides;
    for (const auto &p : configuration.toolbarItems) {
        if (Id(EditorText(p.id)).empty() || !toolbarOverrides.insert(p.id).second || (p.fields & ~127u))
            throw std::invalid_argument("Invalid toolbar override");
        auto found = toolbarEntries.find(p.id);
        if (found == toolbarEntries.end()) {
            if ((p.fields & (ToolbarLocation | ToolbarAction | ToolbarPosition | ToolbarSize)) !=
                (ToolbarLocation | ToolbarAction | ToolbarPosition | ToolbarSize))
                throw std::invalid_argument("Incomplete new toolbar item");
            found = toolbarEntries.emplace(p.id, ToolbarEntry{}).first;
            found->second.id = p.id;
        }
        auto &item = found->second;
        if (p.fields & ToolbarLocation && item.location != p.location) {
            if (toolbars.count(item.location) && toolbars.count(p.location) &&
                String(toolbars.at(item.location).view) != String(toolbars.at(p.location).view)) {
                item.draw = nullptr;
                item.data = nullptr;
            }
            item.location = p.location;
        }
        if (p.fields & ToolbarAction && item.action != p.action) {
            item.action = p.action;
            item.arguments.clear();
            item.checked.clear();
            item.draw = nullptr;
            item.data = nullptr;
        }
        if (p.fields & ToolbarArguments) item.arguments = p.arguments;
        if (p.fields & ToolbarTitle) item.title = p.title;
        if (p.fields & ToolbarPosition) {
            item.x = p.x;
            item.y = p.y;
        }
        if (p.fields & ToolbarSize) {
            item.width = p.width;
            item.height = p.height;
        }
        if ((p.fields & ToolbarVisibility) && !p.visible) toolbarEntries.erase(found);
    }
    std::vector<ToolbarEntry> toolbarItems;
    for (auto &entry : toolbarEntries) {
        auto &item = entry.second;
        const auto original = toolbarDescriptors.find(item.id);
        // Declared controls can outlive an optional action module. Keep
        // their action ID and draw them disabled until it returns. New or
        // replaced actions must resolve, so configuration typos still fail.
        const bool declaredAction =
            original != toolbarDescriptors.end() && String(original->second->action) == item.action;
        const bool declaredCheck =
            original != toolbarDescriptors.end() && String(original->second->checked) == item.checked;
        if (!toolbars.count(item.location) ||
            (!item.action.empty() && !commands.count(item.action) && !declaredAction) ||
            (!item.checked.empty() && !queries.count(item.checked) && !declaredCheck) || std::abs(item.x) > 1000000 ||
            std::abs(item.y) > 1000000 || item.width > 100000 || item.height > 100000 || !std::isfinite(item.x) ||
            !std::isfinite(item.y) || !std::isfinite(item.width) || !std::isfinite(item.height) || item.width <= 0 ||
            item.height <= 0)
            throw std::invalid_argument("Unknown toolbar action/location or invalid geometry: " + item.id);
        const auto args = BorrowEditorFields(item.arguments);
        std::vector<EditorField> copiedArgs;
        if (CopyEditorFields(copiedArgs, args.data(), uint32_t(args.size())) != EDITOR_OK ||
            !EveryField(item.arguments,
                        [](const auto &f) { return f.type != EDITOR_OBJECT && f.type != EDITOR_OBJECT_LIST; }))
            throw std::invalid_argument("Invalid toolbar arguments");
        if (!item.action.empty() && commands.count(item.action)) {
            const auto *c = commands.at(item.action);
            for (const auto &field : item.arguments) {
                bool matches = false;
                for (uint32_t i = 0; i < c->count; ++i)
                    matches |= field.key == String(c->parameters[i].key) && field.type == c->parameters[i].type;
                if (!matches) throw std::invalid_argument("Toolbar action arguments do not match");
            }
            for (uint32_t i = 0; i < c->count; ++i)
                if (!c->parameters[i].optional &&
                    std::none_of(item.arguments.begin(), item.arguments.end(),
                                 [&](const auto &field) { return field.key == String(c->parameters[i].key); }))
                    throw std::invalid_argument("Toolbar action requires arguments");
        }
        toolbarItems.push_back(std::move(item));
    }
    std::set<std::string> placed;
    for (auto &p : configuration.menus) {
        auto item = std::find_if(menus.begin(), menus.end(), [&](const MenuEntry &e) { return e.id == p.id; });
        if (item == menus.end() || !placed.insert(p.id).second || !selected.locations.count(p.location))
            throw std::invalid_argument("Invalid menu placement");
        item->location = p.location;
        item->order = p.order;
    }
    std::set<std::pair<std::string, std::string>> captions;
    for (const auto &menu : menus)
        if (commands.count(menu.action) && !captions.emplace(menu.location, menu.label).second)
            throw std::invalid_argument("Duplicate command menu caption");
    std::set<std::string> bound;
    for (auto &b : configuration.bindings) {
        if (!actionExists(b.first)) throw std::invalid_argument("Invalid binding override");
        if (bound.insert(b.first).second)
            bindings.erase(
                std::remove_if(bindings.begin(), bindings.end(), [&](const auto &v) { return v.first == b.first; }),
                bindings.end());
        if (!b.second.empty()) bindings.push_back(b);
    }
    std::map<std::string, std::vector<EditorKey>> shortcuts;
    std::vector<std::pair<std::string, EditorKey>> used;
    for (auto &b : bindings) {
        EditorKey key;
        if (!ReadEditorKey(b.second, key)) throw std::invalid_argument("Invalid shortcut");
        const auto view = bindingViews[b.first];
        if (std::find(used.begin(), used.end(), std::make_pair(view, key)) != used.end())
            throw std::invalid_argument("Conflicting shortcut");
        shortcuts[b.first].push_back(key);
        used.push_back({view, key});
    }
    for (auto &item : toolbarItems) {
        item.tooltip = item.title;
        const auto chord = std::find_if(bindings.begin(), bindings.end(),
                                        [&](const auto &binding) { return binding.first == item.action; });
        if (chord != bindings.end()) item.tooltip += " (" + chord->second + ")";
    }
    std::stable_sort(menus.begin(), menus.end(), [](const MenuEntry &a, const MenuEntry &b) {
        return a.location == b.location ? (a.order == b.order ? a.id < b.id : a.order < b.order)
                                        : a.location < b.location;
    });
    selected.commands = std::move(commands);
    selected.queries = std::move(queries);
    selected.editors = std::move(editors);
    selected.views = std::move(views);
    selected.tools = std::move(tools);
    selected.themes = std::move(themes);
    selected.fonts = std::move(fonts);
    selected.images = std::move(images);
    selected.documentData = std::move(documentData);
    selected.dataTypes = m_Current.dataTypes;
    selected.themeValues = std::move(themeValues);
    selected.toolbars = std::move(toolbars);
    selected.toolbarItems = std::move(toolbarItems);
    selected.bindingViews = std::move(bindingViews);
    selected.bindingPhases = std::move(bindingPhases);
    selected.properties = std::move(properties);
    selected.menus = std::move(menus);
    selected.bindings = std::move(bindings);
    selected.shortcuts = std::move(shortcuts);
    selected.configuration = configuration;
    selected.appearanceRevision =
        m_Current.appearanceRevision + (m_Current.themes != selected.themes || m_Current.fonts != selected.fonts ||
                                        m_Current.images != selected.images ||
                                        m_Current.themeValues != selected.themeValues);
    return selected;
}
} // namespace vdev::detail
