#pragma once

#include <string>
#include "core/runtime/CkRuntimeHost.h"

namespace vdev {

// File > Save Version and Revert to Saved (Dev.exe sub_5C8BC0, sub_5C8ED0):
// the name of the composition's version step after (1) or before (-1) path.
// The digits before the extension are its number (none: 0), replaced by the
// number plus step on as many digits (at least 2), after a '_' added when
// the name has none there: "Scene.cmo" gives "Scene_01.cmo", "Scene_09.cmo"
// "Scene_10.cmo" and "Scene_08.cmo". Empty when path has no extension.
std::string VersionedPath(const std::string &path, int step);

// File > Create Web Page (Dev.exe sub_598700): the composition saved with the
// page, in its folder with its name and the extension .cmo (Save as Cmo) or
// .vmo.
std::string WebPageCompositionPath(const std::string &htmlPath, bool cmo);

// The page: Dev.exe's HTML template (resource 711, type 23) with its title,
// the composition's file name and the player's size; the player's version
// in its download addresses supplied by the validated runtime metadata.
std::string WebPageHtml(const std::string &title, const std::string &compositionPath, int width, int height,
                        int major, int minor);

// A file saved for the player (CKFILE_FORVIEWER in its header: Export to
// Virtools Player): its Scripts have no Schematic, rebuilt when it opens.
bool IsPlayerFile(const std::string &path);

} // namespace vdev
