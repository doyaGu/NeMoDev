#pragma once

#include "workspace/EditorClient.h"
#include <memory>

namespace vdev {
class DuplicateNamesDialog {
public:
    DuplicateNamesDialog();
    ~DuplicateNamesDialog();
    void Draw(const EditorClient &, uint64_t input);
    void Reset();
private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
