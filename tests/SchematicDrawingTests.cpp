// This executable compiles and links the actual painter without the SDK.
#include "ui/schematic/SchematicRenderer.h"
#include "ui/schematic/Snapshots.h"
#include "imgui_internal.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace {
unsigned imageReads = 0;
void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::vector<ImDrawVert> Draw(const vdev::SchematicModel &model, const vdev::DrawOptions &options) {
    ImDrawListSharedData shared;
    shared.SetCircleTessellationMaxError(0.3f);
    shared.CurveTessellationMaxError = 1.25f;
    ImDrawList list(&shared);
    list._ResetForNewFrame();
    list._SetPixelDensity(1.0f);
    list.PushClipRect(options.clipMin, options.clipMax);
    list.PushTexture(ImTextureID());
    vdev::DrawScript(&list, model, 0, {}, {}, options);
    return {list.VtxBuffer.begin(), list.VtxBuffer.end()};
}
size_t Colored(const std::vector<ImDrawVert> &vertices, ImU32 color) {
    return std::count_if(vertices.begin(), vertices.end(), [&](const auto &v) { return v.col == color; });
}
} // namespace
namespace vdev {
// The painter only passes owned image data to its UI texture adapter.
ImTextureID SnapshotTexture(const std::shared_ptr<const SchematicSnapshot> &image, ImVec2 *size) {
    Check(image && image->width == 1 && image->height == 1 &&
              image->pixels == std::vector<unsigned char>({1, 2, 3, 255}),
          "painter did not retain copied snapshot pixels");
    ++imageReads;
    if (size)
        *size = {1, 1};
    return {};
}
} // namespace vdev
int main() {
    using namespace vdev;
    SchematicModel model;
    model.script.id = 1;
    model.script.ref = {ElementKind::Script, 1, 1};
    model.script.bounds = {0, 0, 600, 240};
    model.script.header = {0, 0, 140, 240};
    BehaviorItem behavior;
    behavior.id = 2;
    behavior.parent = 1;
    behavior.ref = {ElementKind::Behavior, 1, 2};
    behavior.box = {240, 80, 120, 40};
    model.behaviors.push_back(behavior);
    LinkItem link;
    link.owner = 1;
    link.ref = {ElementKind::Link, 1, 3, 0};
    link.resolved = link.behaviorLink = true;
    link.path = {{160, 100}, {240, 100}};
    model.links.push_back(link);
    model.Reindex();
    SchematicStyle style;
    DrawOptions options;
    options.style = &style;
    options.clipMin = {0, 0};
    options.clipMax = {600, 240};
    const auto normal = Draw(model, options);
    Check(!normal.empty(), "empty Schematic drawing");
    const ImU32 orange = IM_COL32(255, 140, 0, 255), red = IM_COL32(230, 0, 0, 255);
    Check(!Colored(normal, orange) && !Colored(normal, red), "stopped Schematic contains runtime marks");
    SchematicActivity activity;
    options.activity = &activity;
    activity.running.insert(behavior.ref);
    Check(Colored(Draw(model, options), orange) > 0, "running behavior lost its orange frame");
    activity.current = behavior.ref;
    const auto current = Draw(model, options);
    Check(Colored(current, red) > 0 && !Colored(current, orange), "debug behavior lost its red frame");
    activity.current = {};
    activity.trace = true;
    activity.firing.insert(link.ref);
    const auto trace = Draw(model, options);
    Check(Colored(trace, red) > Colored(current, red) && !Colored(trace, orange), "Trace lost behavior or link marks");
    auto wrongBehavior = behavior.ref, wrongLink = link.ref;
    ++wrongBehavior.owner;
    ++wrongLink.index;
    activity.running = {wrongBehavior};
    activity.current = wrongBehavior;
    activity.firing = {wrongLink};
    Check(!Colored(Draw(model, options), red), "painter matched only a native object number");
    options.activity = nullptr;
    std::set<ElementRef> invalid{wrongLink};
    options.invalid = &invalid;
    Check(!Colored(Draw(model, options), style.invalidLink), "invalid mark moved to another link slot");
    invalid = {link.ref};
    Check(Colored(Draw(model, options), style.invalidLink) > 0, "invalid link color was lost");
    options.invalid = nullptr;
    for (auto kind : {SchematicOwnerKind::Object, SchematicOwnerKind::Level, SchematicOwnerKind::Place,
                      SchematicOwnerKind::Scene, SchematicOwnerKind::Script, SchematicOwnerKind::Entity2D,
                      SchematicOwnerKind::Entity3D, SchematicOwnerKind::Material}) {
        model.script.ownerKind = kind;
        Check(!Draw(model, options).empty(), "copied owner category could not be drawn");
    }
    auto image = std::make_shared<SchematicSnapshot>();
    image->width = image->height = 1;
    image->pixels = {1, 2, 3, 255};
    model.script.snapshot = image;
    image.reset();
    Draw(model, options);
    Check(imageReads == 1, "copied snapshot was not presented");
    return 0;
}
