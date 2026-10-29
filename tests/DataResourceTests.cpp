#include "workspace/ResourceDrag.h"
#include <stdexcept>
namespace vdev { class CkRuntimeHost; }
namespace {
void Check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
}
void TestDataResources(vdev::CkRuntimeHost &) {
    using namespace vdev;
    const std::vector<ResourceFile> files{{std::string(3000, 'x') + ".nms", 8}, {"Second.nms", 8}};
    const auto bytes = WriteResourceDrag(1, files);
    uint32_t source = 9;
    std::vector<ResourceFile> decoded{{"Keep", 0}};
    Check(ReadResourceDrag(bytes.data(), bytes.size(), source, decoded) && source == 1 && decoded.size() == 2 &&
              decoded[0].path == files[0].path,
          "resource drag truncated paths or source");
    for (size_t size : {size_t(0), bytes.size() - 1, bytes.size() + 1})
        Check(!ReadResourceDrag(bytes.data(), size, source, decoded) && source == 1 && decoded.size() == 2,
              "malformed resource drag changed previous output");
    Check(!ReadResourceDrag(nullptr, bytes.size(), source, decoded) && WriteResourceDrag(2, files).empty() &&
              WriteResourceDrag(0, {}).empty(),
          "invalid resource drag accepted");
}
