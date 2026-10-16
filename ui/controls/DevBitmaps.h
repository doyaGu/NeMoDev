#pragma once

#include <set>
#include <string>

#include "imgui.h"

#include "CKAll.h"

namespace vdev {

// The ui group: Dev's interface bitmaps, those of resvis.dll (Dev.exe
// sub_42A2E0 loads it) the editor draws, built into the program
// (resources/images). Toolbar buttons name their bitmaps by resource id
// (Dev.exe sub_4B6C70 for the 3D Layout).
// The texture of a bitmap, made on first use; null when the bitmap is
// missing. size: the bitmap's size in pixels.
ImTextureID DevBitmap(int id, ImVec2 *size = nullptr);
// A bitmap of Managers\CK2UI.dll (the parameter UIs' pick and expand buttons).
ImTextureID CK2UIBitmap(int id, ImVec2 *size = nullptr);
// An 11 x 11 icon of the ui group (resvis.dll: the image list of Dev's tree views, Dev.exe
// LoadIcons), its mask as alpha.
ImTextureID DevIconImage(int id);
// An 11 x 11 icon of the app group (Dev.exe itself (AfxGetModuleState's instance: the
// Level Manager's cells and its tree's arrows, Dev.exe sub_5687A0).
ImTextureID DevExeIcon(int id);
// A 32 x 32 icon of the ui group: the icons of Dev's messages (Dev.exe
// sub_5A07E0's dialog, icons 0x525 to 0x52A).
ImTextureID DevMessageIcon(int id);
// The ui icon of a class in Dev's lists (Dev.exe sub_5D1F80, the
// index in LoadIcons' list).
int DevClassIconId(CK_CLASSID cid);
// The bitmaps and icons asked for that the program does not have (named as
// in resources/resources.rc): ones resources/images.txt should list.
const std::set<std::string> &MissingResources();
void ReleaseDevBitmaps();

// One of Dev's bitmap buttons (VIBitmapButton) at a screen point: its
// bitmaps normal, hover, checked (or held), checked hover, disabled, drawn
// scale times their size. rightClicked: set on a right click (then false is
// returned).
bool DevBitmapButton(const char *id, const ImVec2 &at, const int bitmaps[5], bool checked, bool enabled, float scale,
                     const char *tooltip, bool *rightClicked = nullptr);

} // namespace vdev
