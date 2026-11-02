#pragma once

#include "application/EditorApplication.h"

namespace vdev {

// A startup source contributes through EditorApplication::Register. Linked
// features and library entries share validation, activation and rollback.
struct EditorModuleSource {
    std::string name;
    std::function<bool(EditorApplication &, std::string &)> collect;
    std::string file;
};

struct EditorModuleSetting {
    std::string id;
    bool enabled = true;
    std::string path;
    bool builtin = false;
};
struct EditorModuleInfo {
    std::string id, file;
    bool enabled = true;
};

struct EditorModuleDirectory {
    std::string path;
    bool required = false;
};

// The binary's directory, or Contents/PlugIns/EditorModules in an app bundle.
// No current-working-directory or native-runtime search is performed.
std::string EditorModuleDirectoryAt(const std::string &basePath);

// Immediate platform libraries only, in directory/name order, followed by
// explicit files. Canonical aliases and hard links load once. Failure leaves
// the output unchanged. Missing optional directories are allowed.
bool DiscoverEditorModules(const std::vector<EditorModuleDirectory> &directories,
                           const std::vector<std::string> &files,
                           std::vector<EditorModuleSource> &out, std::string &error);

} // namespace vdev
