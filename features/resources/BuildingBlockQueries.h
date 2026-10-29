#pragma once
#include "application/EditorApplication.h"
#include "core/catalog/BuildingBlockCatalog.h"

namespace vdev {
bool RegisterBuildingBlockQueries(EditorApplication &, const BuildingBlockCatalog &, std::string &error);
}
