#include "core/document/OutputFiles.h"
#include "core/document/detail/PendingSaveFile.h"

#include <algorithm>
#include <memory>
#include <array>
#include <fstream>

namespace {
struct FileVersion {
    bool exists = false;
    uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
    uint64_t contents = 14695981039346656037ull;
};
bool ReadVersion(const std::filesystem::path &path, FileVersion &version) {
    std::error_code error;
    version.exists = std::filesystem::exists(path, error);
    if (error || !version.exists) return !error;
    if (!std::filesystem::is_regular_file(path, error) || error) return false;
    version.size = std::filesystem::file_size(path, error);
    if (error) return false;
    version.modified = std::filesystem::last_write_time(path, error);
    if (error) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::array<char, 65536> bytes;
    while (input.read(bytes.data(), bytes.size()) || input.gcount())
        for (std::streamsize i = 0; i < input.gcount(); ++i) {
            version.contents ^= static_cast<unsigned char>(bytes[size_t(i)]);
            version.contents *= 1099511628211ull;
        }
    return input.eof();
}
bool Matches(const std::filesystem::path &path, const FileVersion &before) {
    FileVersion now;
    return ReadVersion(path, now) && before.exists == now.exists &&
        (!now.exists || (before.size == now.size && before.modified == now.modified && before.contents == now.contents));
}
}

namespace vdev {
struct OutputFiles::Data {
    struct File {
        std::string name;
        std::unique_ptr<PendingSaveFile> staged, backup;
        bool existed = false, published = false;
        FileVersion before, after;
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
            bool restored = false;
            std::string failure;
            try {
                if (!Matches(i->staged->Destination(), i->after))
                    failure = "The output was changed by another writer";
                else if (i->existed) restored = i->backup->Publish(true);
                else {
                    const auto error = operations.Remove(i->staged->Destination());
                    restored = !error;
                    if (error) failure = error.message();
                }
            } catch (const std::exception &error) {
                failure = error.what();
            } catch (...) {
                failure = "The file operation raised an exception";
            }
            if (restored)
                i->published = false;
            else {
                report.errors.push_back(std::string(i->existed ? "Cannot restore the previous output: " :
                                                               "Cannot remove the new output: ") + i->name +
                                        (failure.empty() ? std::string() : ": " + failure));
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
    FileVersion version;
    if (!ReadVersion(pending->Destination(), version)) {
        d.report.errors.push_back("Cannot inspect the previous output: " + destination);
        d.valid = false;
        return {};
    }
    d.files.push_back({destination, std::move(pending), {}, version.exists, false, version, {}});
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
            if (!Matches(file.staged->Destination(), file.before)) {
                d.report.errors.push_back("The output was changed by another writer: " + file.name);
                return false;
            }
            if (!file.staged->Publish(file.existed)) return false;
            written.push_back(std::move(file.name));
            return true;
        }
        // No destination changes until every old file has a recoverable copy.
        for (auto &file : d.files) {
            std::error_code error;
            if (!Matches(file.staged->Destination(), file.before)) {
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
            if (!Matches(file.staged->Destination(), file.before)) {
                d.report.errors.push_back("The output was changed by another writer: " + file.name);
                d.Restore(written);
                return false;
            }
            if (!ReadVersion(file.staged->Path(), file.after)) {
                d.report.errors.push_back("Cannot inspect the prepared output: " + file.name);
                d.Restore(written);
                return false;
            }
            if (!file.staged->Publish(file.existed)) {
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
    } catch (...) {
        d.report.errors.push_back("Cannot publish output files: the file operation raised an exception");
        d.Restore(written);
        return false;
    }
}
} // namespace vdev
