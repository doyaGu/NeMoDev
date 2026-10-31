#include "core/document/SaveFileOperations.h"

#include <cerrno>
#include <cstdio>
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

namespace vdev {

SaveFileOperations &SaveFileOperations::Native() {
    static SaveFileOperations operations;
    return operations;
}

std::error_code SaveFileOperations::Replace(const std::filesystem::path &source,
                                           const std::filesystem::path &destination, bool overwrite) {
#ifdef _WIN32
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(source.c_str(), destination.c_str(), flags))
        return {int(GetLastError()), std::system_category()};
#else
    int result = 0;
    if (overwrite) result = rename(source.c_str(), destination.c_str());
#if defined(__linux__)
    else result = int(syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE));
#elif defined(__APPLE__)
    else result = renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL);
#else
    else result = link(source.c_str(), destination.c_str());
#endif
    if (result != 0) return {errno, std::generic_category()};
#endif
    return {};
}

std::error_code SaveFileOperations::Remove(const std::filesystem::path &path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    return error;
}

} // namespace vdev
