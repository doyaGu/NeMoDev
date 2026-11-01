#pragma once
#include "application/EditorApplication.h"
#include <memory>

namespace vdev {
class LayoutView;
class ObjectSelection;
struct LayoutInterfaceData;
struct LayoutRestoreData;

// Scene information and editing used by the Layout's menus and object lists.
// The rendering target remains owned by LayoutView and its UI renderer.
class LayoutCommands {
public:
    LayoutCommands(EditorApplication &, std::function<Document *()>,
                   std::function<LayoutView &()>, ObjectSelection &);
    ~LayoutCommands();
    bool Register(std::string &error);
    void Poll();
    LayoutRestoreData ProjectInterface(const LayoutInterfaceData &) const;
    static const std::vector<ProviderChoice> &DefaultProviders();
private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
