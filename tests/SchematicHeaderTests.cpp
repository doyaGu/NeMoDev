// This target has no SDK include directories or libraries.
#include "core/schematic/SchematicModel.h"
#include "core/schematic/SchematicNavigation.h"
#include "core/schematic/SchematicSearch.h"
#include "features/schematic/SchematicModelData.h"
#include <type_traits>

static_assert(std::is_same_v<decltype(vdev::ScriptItem::id), uint32_t>);
static_assert(std::is_same_v<decltype(vdev::ScriptItem::snapshot), std::shared_ptr<const vdev::SchematicSnapshot>>);
static_assert(std::is_same_v<decltype(vdev::SearchResult::object), uint32_t>);
static_assert(std::is_copy_constructible_v<vdev::SchematicModel>);
