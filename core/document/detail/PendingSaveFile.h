#pragma once

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <random>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif

#include "core/document/DocumentSaver.h"

namespace vdev {

// Owns only the candidate file. The destination is never removed before a
// successful same-directory publication. Callers write and verify Path()
// before publishing; cleanup failures are recorded in the caller's report.
class PendingSaveFile {
public:
    PendingSaveFile(const std::filesystem::path &destination, SaveReport &report)
        : m_Destination(destination), m_Report(report) {}
    PendingSaveFile(const PendingSaveFile &) = delete;
    PendingSaveFile &operator=(const PendingSaveFile &) = delete;
    ~PendingSaveFile() {
        if (m_Path.empty())
            return;
        std::error_code error;
        std::filesystem::remove(m_Path, error);
        if (error)
            m_Report.notes.push_back("Cannot remove temporary save file " + m_Path.string() + ": " + error.message());
    }
    bool Create() {
        namespace fs = std::filesystem;
        std::error_code ec;
        m_Destination = fs::absolute(m_Destination, ec);
        if (ec) return Fail("Cannot resolve the output path", ec);
        // Follow an existing target link as the previous direct write did. A
        // candidate belongs beside the actual destination, on the same filesystem.
        m_Destination = fs::weakly_canonical(m_Destination, ec);
        if (ec) {
            m_Report.errors.push_back("Cannot resolve the output directory: " + ec.message());
            return false;
        }
        // weakly_canonical cannot follow a dangling final symlink. Preserve the old
        // file API's behavior of creating its referent, rather than replacing the link.
        for (int links = 0; fs::is_symlink(fs::symlink_status(m_Destination, ec)); ++links) {
            if (links == 40) {
                m_Report.errors.push_back("Too many symbolic links in the output path");
                return false;
            }
            const auto link = fs::read_symlink(m_Destination, ec);
            if (!ec) m_Destination = fs::weakly_canonical(link.is_absolute() ? link : m_Destination.parent_path() / link, ec);
            if (ec) {
                m_Report.errors.push_back("Cannot resolve the output link: " + ec.message());
                return false;
            }
        }
        std::random_device random;
        for (int attempt = 0; attempt < 64; ++attempt) {
            std::filesystem::path name(".nemodev-save-" + std::to_string(random()) + "-" + std::to_string(random()));
            name += m_Destination.extension();
            auto candidate = m_Destination.parent_path() / name;
#ifdef _WIN32
            HANDLE file =
                CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
                    continue;
                return Fail("Cannot create a temporary save file in the destination directory",
                            std::error_code(error, std::system_category()));
            }
            CloseHandle(file);
#else
            const int file = open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
            if (file < 0) {
                const int error = errno;
                if (error == EEXIST)
                    continue;
                return Fail("Cannot create a temporary save file in the destination directory",
                            std::error_code(error, std::generic_category()));
            }
            close(file);
#endif
            m_Path = std::move(candidate);
            return PreservePermissions();
        }
        m_Report.errors.push_back("Cannot choose an unused temporary save file name");
        return false;
    }
    const std::filesystem::path &Path() const { return m_Path; }
    bool Publish(bool allowOverwrite) {
        if (allowOverwrite && !PreservePermissions())
            return false;
#ifdef _WIN32
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (allowOverwrite ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!MoveFileExW(m_Path.c_str(), m_Destination.c_str(), flags))
            return Fail("Cannot replace the destination file",
                        std::error_code(GetLastError(), std::system_category()));
        m_Path.clear();
#else
        if (allowOverwrite) {
            std::error_code error;
            const auto status = std::filesystem::status(m_Destination, error);
            if (error && error != std::errc::no_such_file_or_directory)
                return Fail("Cannot inspect the destination file", error);
            if (std::filesystem::exists(status)) {
                if (access(m_Destination.c_str(), W_OK) != 0)
                    return Fail("Cannot write the destination file",
                                std::error_code(errno, std::generic_category()));
            }
            if (rename(m_Path.c_str(), m_Destination.c_str()) != 0)
                return Fail("Cannot replace the destination file",
                            std::error_code(errno, std::generic_category()));
            m_Path.clear();
        } else {
            // Refuse an existing destination, including one created during CK
            // callbacks. These operations do not need filesystem hard-link support.
#if defined(__linux__)
            if (syscall(SYS_renameat2, AT_FDCWD, m_Path.c_str(), AT_FDCWD, m_Destination.c_str(), RENAME_NOREPLACE) !=
                0)
                return Fail("Cannot create the destination file",
                            std::error_code(errno, std::generic_category()));
            m_Path.clear();
#elif defined(__APPLE__)
            if (renamex_np(m_Path.c_str(), m_Destination.c_str(), RENAME_EXCL) != 0)
                return Fail("Cannot create the destination file",
                            std::error_code(errno, std::generic_category()));
            m_Path.clear();
#else
            if (link(m_Path.c_str(), m_Destination.c_str()) != 0)
                return Fail("Cannot create the destination file",
                            std::error_code(errno, std::generic_category()));
                // The remaining candidate name is removed by this owner's destructor.
#endif
        }
#endif
        return true;
    }

private:
    std::filesystem::path m_Destination, m_Path;
    SaveReport &m_Report;
    bool PreservePermissions() {
#ifdef _WIN32
        DWORD size = 0;
        GetFileSecurityW(m_Destination.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return true;
        if (error != ERROR_INSUFFICIENT_BUFFER)
            return Fail("Cannot read the destination file permissions",
                        std::error_code(error, std::system_category()));
        std::vector<unsigned char> security(size);
        auto *descriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(security.data());
        if (!GetFileSecurityW(m_Destination.c_str(), DACL_SECURITY_INFORMATION, descriptor, size, &size))
            return Fail("Cannot read the destination file permissions",
                        std::error_code(GetLastError(), std::system_category()));
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        if (!GetSecurityDescriptorControl(descriptor, &control, &revision))
            return Fail("Cannot read the destination file permissions",
                        std::error_code(GetLastError(), std::system_category()));
        const SECURITY_INFORMATION inheritance =
            control & SE_DACL_PROTECTED ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;
        if (!SetFileSecurityW(m_Path.c_str(), DACL_SECURITY_INFORMATION | inheritance, descriptor))
            return Fail("Cannot preserve the destination file permissions",
                        std::error_code(GetLastError(), std::system_category()));
#else
        std::error_code error;
        const auto status = std::filesystem::status(m_Destination, error);
        if (error && error != std::errc::no_such_file_or_directory)
            return Fail("Cannot read the destination file permissions", error);
        if (std::filesystem::exists(status)) {
            std::filesystem::permissions(m_Path, status.permissions(), error);
            if (error)
                return Fail("Cannot preserve the destination file permissions", error);
        }
#endif
        return true;
    }
    bool Fail(const char *message, const std::error_code &error) {
        m_Report.errors.push_back(std::string(message) + ": " + error.message());
        return false;
    }
};

} // namespace vdev

