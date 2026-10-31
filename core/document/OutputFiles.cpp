#include "core/document/OutputFiles.h"
#include "core/document/detail/PendingSaveFile.h"

#include <algorithm>
#include <memory>

namespace vdev {
struct OutputFiles::Data {
    struct File {
        std::string name;
        std::unique_ptr<PendingSaveFile> staged, backup;
        bool existed = false, published = false;
    };
    SaveReport &report;
    SaveFileOperations &operations;
    std::vector<File> files;
    bool attempted = false;
    bool valid = true;
    Data(SaveReport &report, SaveFileOperations *operations)
        : report(report), operations(operations ? *operations : SaveFileOperations::Native()) {}
    void Restore(std::vector<std::string> &written) {
        for (auto i = files.rbegin(); i != files.rend(); ++i) {
            if (!i->published) continue;
            std::error_code error;
            const bool restored = i->existed ? i->backup->Publish(true)
                                             : (std::filesystem::remove(i->staged->Destination(), error), !error);
            if (restored)
                i->published = false;
            else {
                report.errors.push_back("Cannot restore the previous output: " + i->name);
                if (i->existed)
                    report.notes.push_back("Previous output saved at " + i->backup->LeaveForRecovery().u8string());
            }
        }
        for (const auto &file : files)
            if (file.published) written.push_back(file.name);
    }
};
OutputFiles::OutputFiles(SaveReport &report, SaveFileOperations *operations)
    : m_Data(std::make_unique<Data>(report, operations)) {}
OutputFiles::~OutputFiles() = default;
std::string OutputFiles::Add(const std::string &destination) {
    auto &d = *m_Data;
    if (d.attempted || !d.valid || destination.empty()) {
        d.valid = false;
        return {};
    }
    auto pending = std::make_unique<PendingSaveFile>(std::filesystem::u8path(destination), d.report, &d.operations);
    if (!pending->Create()) {
        d.valid = false;
        return {};
    }
    for (const auto &file : d.files) {
        std::error_code error;
        if (file.staged->Destination() == pending->Destination() ||
            std::filesystem::equivalent(file.staged->Destination(), pending->Destination(), error)) {
            d.report.errors.push_back("Output files need different paths: " + destination);
            d.valid = false;
            return {};
        }
    }
    auto path = pending->Path().u8string();
    d.files.push_back({destination, std::move(pending), {}, false, false});
    return path;
}
bool OutputFiles::Publish(std::vector<std::string> &written) {
    auto &d = *m_Data;
    if (d.attempted || !d.valid || d.files.empty()) return false;
    d.attempted = true;
    written.reserve(written.size() + d.files.size());
    try {
        // A single file already has atomic same-directory replacement. It
        // needs no backup or compensating write, and keeps the original path.
        if (d.files.size() == 1) {
            auto &file = d.files.front();
            if (!file.staged->Publish(true)) return false;
            written.push_back(std::move(file.name));
            return true;
        }
        // No destination changes until every old file has a recoverable copy.
        for (auto &file : d.files) {
            std::error_code error;
            file.existed = std::filesystem::exists(file.staged->Destination(), error);
            if (error || (file.existed && !std::filesystem::is_regular_file(file.staged->Destination(), error))) {
                d.report.errors.push_back("Cannot preserve the previous output: " + file.name);
                return false;
            }
            if (!file.existed) continue;
            file.backup = std::make_unique<PendingSaveFile>(file.staged->Destination(), d.report, &d.operations);
            if (!file.backup->Create()) return false;
            // Create preserves destination permissions. A read-only source is
            // still readable for backup; make our private candidate writable
            // while copying. copy_file reapplies the source's permissions.
            std::filesystem::permissions(file.backup->Path(), std::filesystem::perms::owner_write,
                                         std::filesystem::perm_options::add, error);
            if (error || !std::filesystem::copy_file(file.staged->Destination(), file.backup->Path(),
                                                     std::filesystem::copy_options::overwrite_existing, error)) {
                d.report.errors.push_back("Cannot preserve the previous output: " + file.name + ": " + error.message());
                return false;
            }
        }
        for (auto &file : d.files) {
            if (!file.staged->Publish(true)) {
                d.Restore(written);
                return false;
            }
            file.published = true;
        }
        for (auto &file : d.files) written.push_back(std::move(file.name));
        return true;
    } catch (const std::exception &error) {
        d.report.errors.push_back(std::string("Cannot publish output files: ") + error.what());
        d.Restore(written);
        return false;
    }
}
} // namespace vdev
