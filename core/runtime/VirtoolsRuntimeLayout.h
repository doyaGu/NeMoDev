#pragma once

#include <string>
#include <vector>

class CKPluginManager;

// The directory of a runtime's CK2.dll: path, or its Bin folder (a game such
// as Ballance has CK2.dll in Bin\); path when neither has it. The editor's
// --runtime (ui/app/Main.cpp) and the build's VIRTOOLS_RUNTIME_PATH take a
// game folder the same way. A macOS application bundle keeps its runtime in
// Contents/Frameworks; its Resources and MacOS directories resolve there too.
std::string VirtoolsRuntimeDirectory(const std::string &path);

// Where a Virtools runtime keeps its plugins. A Dev install has CK2.dll and
// the plugin folders (RenderEngines, Managers, BuildingBlocks, Plugins) in
// one directory; a game such as Ballance has CK2.dll in Bin\ and the plugin
// folders in the directory above. runtimeDirectory is the one holding
// CK2.dll; the result is it, or its parent when only the parent has a
// RenderEngines folder, or Contents/PlugIns in a macOS application bundle.
std::string VirtoolsPluginRoot(const std::string &runtimeDirectory);

// The plugin folders, in the SDK Standalone Player's order.
extern const char *const VIRTOOLS_PLUGIN_FOLDERS[4];

// A DLL of a plugin folder that is not loaded: Ballance's mod loader
// (BuildingBlocks\BMLPlus.dll, AngelScript.dll) is not part of the game's
// runtime. fileName: a name or a path; compared without case.
bool VirtoolsIsIgnoredPlugin(const std::string &fileName);

// CKPluginManager::ParsePlugins without the ignored DLLs: the folder added to
// the library search path, then each *.dll of it and its subfolders
// registered (CK2's ParsePlugins: a CKDirectoryParser, recursive, and
// RegisterPlugin). Returns the DLLs registered; skipped: the paths left out.
int VirtoolsParsePlugins(CKPluginManager *plugins, const std::string &directory,
                         std::vector<std::string> *skipped = nullptr);

// The four folders under the plugin root of runtimeDirectory parsed with
// VirtoolsParsePlugins. Returns the DLLs registered.
int VirtoolsParseRuntimePlugins(CKPluginManager *plugins, const std::string &runtimeDirectory,
                                std::vector<std::string> *skipped = nullptr);
