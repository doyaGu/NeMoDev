#pragma once

#include <map>
#include <string>
#include <vector>

#include <cstdint>

namespace vdev {

struct EditorField;
bool PreferenceNativeText(const std::string &key);
struct PreferencePage { const char *id; const char *title; };

// Dev 2.5 "General Preferences: Schematic" (UI reference 3.4.6). Defaults
// are Dev's: static values of Dev.exe (read in 0x512DF0 from
// HKCU\Software\Virtools\Schematic) and the values Dev wrote there. Colors
// are GDI COLORREF (0x00BBGGRR): Parameter Link 0xC8323C is the blue of
// Dev's parameter links, the orange Script header is 0x96C8FA.
struct Preferences {
    bool automaticLink = false;     // "Automatic Link"
    bool strictMultiSelection = false;
    int defaultLinkDelay = 0;       // "Default Link Delay"
    bool autoLoopLinkDelay = true;  // "Auto Loop Link Delay"
    bool searchBestPath = true;     // "Search Best Path Link" (value on the reference machine; Ctrl inverts)
    int maxRecentBlocks = 10;       // "Max Recent Building Block Count"
    bool tooltips = true;           // "Enable Tooltips"
    int tooltipDelayMs = 500;       // "Tooltips Delay(ms)"
    bool showScriptLeftPart = true; // "Show Script Left Part" (headers)
    bool showControlPoints = true;
    bool showLinkDelay = true;
    bool showPriorities = false;
    // Script Debugger (Dev registry names; defaults are library choices).
    bool behaviorBreakpointReengage = true;  // "Behavior Breakpoint Auto Reengage"
    bool linkBreakpointReengage = true;      // "Link Breakpoint Auto Reengage"
    bool parameterBreakpointReengage = true; // "Parameter Breakpoint Auto Reengage"
    bool redrawOnlyAtBreakpoint = false;     // "Redraw Schematic Only When Breakpoint Reached"
    // Colors (Dev palette slot in parentheses).
    uint32_t ioLinkColor = 0x000000;            // behavior links (Black)
    uint32_t parameterLinkColor = 0xC8323C;     // (Red)
    uint32_t exportedLinkColor = 0xFFFF00;      // exported parameters (Yellow)
    uint32_t operationLinkColor = 0x408000;     // parameter operation links (DarkGreen)
    uint32_t validLinkColor = 0x64A014;         // link being created (Green)
    uint32_t invalidLinkColor = 0x0000FF;       // (Blue)
    uint32_t obsoleteColor = 0x7D9632;          // obsolete Building Block (GreenYellow)
    uint32_t noPrototypeColor = 0x0080FF;       // (DodgerBlue)
    uint32_t noObjectColor = 0x0000FF;          // behavior without CKObject (Blue2)
    uint32_t toReplaceColor = 0xD2B390;         // newer prototype version (BurlyWood)

    // File menu: the last compositions opened or saved, newest first (Dev
    // lists eight, numbered "1)" to "8)").
    std::vector<std::string> recentFiles;
    void AddRecentFile(const std::string &path);

    // The other General Preferences values (3D Layout pages, Miscellaneous
    // Controls, the rest of the Schematic page, Behavior Link Properties),
    // keyed "<Dev registry key under HKCU\Software\Virtools>\<value name>"
    // (DevSettings()); values as text.
    std::map<std::string, std::string> settings = DefaultSettings();
    bool settingsSaved = false; // the preferences file had them
    static std::map<std::string, std::string> DefaultSettings();
    bool Bool(const std::string &key) const;
    int Int(const std::string &key) const;
    float Float(const std::string &key) const;
    std::string Text(const std::string &key) const;
    void Set(const std::string &key, const std::string &value) { settings[key] = value; }
    void SetBool(const std::string &key, bool value) { settings[key] = value ? "1" : "0"; }
    void SetInt(const std::string &key, int value) { settings[key] = std::to_string(value); }
    void SetFloat(const std::string &key, float value);
    // Dev's Default button: every setting whose key starts with prefix.
    void ResetSettings(const std::string &prefix);

    // Path Manager (CKPathManager categories and their paths, Dev's
    // HKCU\Software\Virtools\Data Paths); pathsSaved: the editor has its own.
    std::vector<std::pair<std::string, std::vector<std::string>>> paths;
    bool pathsSaved = false;
    // Data Resources open in the editor (.rsc files), opened again at start.
    std::vector<std::string> dataResources;

    // The editable General Preferences values; unrelated session data stays owned here.
    std::vector<EditorField> Values(bool generalOnly = true) const;
    bool ReadValues(const std::vector<EditorField> &, std::string &error);
    bool RestoreValues(const std::vector<EditorField> &, std::string &error);
    bool SetValues(const std::vector<EditorField> &, std::string &error);
    void ResetPage(int page, const std::string &firstEngine);
    static const std::vector<PreferencePage> &Pages();

    bool Load(const std::string &path);
    bool Save(const std::string &path) const;
    // Reads Dev's own settings, if present (read-only). Returns the number
    // of values found.
    int ImportFromDev();
    // Only the settings above, from Dev's registry (read-only).
    int ImportDevSettings();
    // Dev's dev.ini of the runtime (<File Options>, <BehaviorManager>), read-only.
    int ImportDevIni(const std::string &runtimeDirectory);
    // Dev's path categories (HKCU\Software\Virtools\Data Paths), read-only.
    bool ImportDevPaths();
private:
    bool DecodeValues(const std::vector<EditorField> &, std::string &error, bool validate, bool generalOnly = true);
};

// A General Preferences value: key, Dev's default, kind ('b' bool, 'i' int,
// 'f' float, 's' text; registry DWORD for b/i, string for f/s).
struct DevSetting {
    const char *key;
    const char *fallback;
    char kind;
};
const std::vector<DevSetting> &DevSettings();

} // namespace vdev
