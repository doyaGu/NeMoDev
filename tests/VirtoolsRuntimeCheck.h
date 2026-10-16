#pragma once

// The tests on the CK runtime load CK2.dll and VxMath.dll delay-loaded, as
// the editor does (virtools_enable_delay_load_runtime): without them the
// program still starts, and skips (77, CTest's SKIP_RETURN_CODE) instead of
// failing to load.

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <string>

namespace vdevtest {

constexpr int SKIPPED = 77;

// The runtime's folder (VIRTOOLS_PATH) has CK2.dll, which then resolves from
// it; without VIRTOOLS_PATH, CK2.dll is looked for as Windows would load it
// (the program's folder, the current one, PATH). why: what is missing.
inline bool RuntimeAvailable(std::string &why) {
#ifdef _WIN32
    if (const char *path = std::getenv("VIRTOOLS_PATH")) {
        std::error_code error;
        const std::filesystem::path directory = std::filesystem::absolute(path, error);
        if (error || !std::filesystem::exists(directory / "CK2.dll", error)) {
            why = "no CK2.dll in VIRTOOLS_PATH (" + std::string(path) + ")";
            return false;
        }
        SetDllDirectoryW(directory.wstring().c_str());
        return true;
    }
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"CK2.dll", nullptr, MAX_PATH, found, nullptr) == 0) {
        why = "the Virtools runtime is not found (set VIRTOOLS_PATH)";
        return false;
    }
    return true;
#else
    // The source runtime is linked and staged with the tests. Missing shared
    // libraries are build/deployment errors, not a new platform skip.
    (void) why;
    return true;
#endif
}

} // namespace vdevtest
