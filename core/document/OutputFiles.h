#pragma once

#include <memory>
#include <string>
#include <vector>

namespace vdev {
struct SaveReport;

// A declared set of files, staged beside their actual destinations. Publish
// preserves previous files first and restores them in reverse order on failure.
// The caller writes and verifies every staged file before publishing. Recovery
// failures report the remaining outputs and retain backups for the user.
class OutputFiles {
  public:
    explicit OutputFiles(SaveReport &report);
    ~OutputFiles();
    OutputFiles(const OutputFiles &) = delete;
    OutputFiles &operator=(const OutputFiles &) = delete;
    std::string Add(const std::string &destination);
    bool Publish(std::vector<std::string> &written);

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
