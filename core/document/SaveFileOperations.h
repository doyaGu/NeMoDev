#pragma once

#include <filesystem>
#include <system_error>

namespace vdev {

// The two filesystem changes used to publish and recover saved outputs.
// Failed operations leave their source and destination unchanged. File owners
// may supply an instance that outlives their staged files and queued work.
class SaveFileOperations {
public:
    virtual ~SaveFileOperations() = default;
    virtual std::error_code Replace(const std::filesystem::path &source,
                                   const std::filesystem::path &destination, bool overwrite);
    virtual std::error_code Remove(const std::filesystem::path &path);
    static SaveFileOperations &Native();
};

} // namespace vdev
