#pragma once

#include "workspace/EditorClient.h"
#include <string>

namespace vdev {
struct StatusBarInfo {
    std::string selection = "Nothing selected", action, information, coordinates[3];
    bool showCoordinates = false;
};

class EditorStatusBar {
public:
    bool Register(EditorApplication &, std::string &);
    void Draw(const EditorClient &, const StatusBarInfo &);
private:
    uint32_t DrawControl(const EditorClient &, const EditorToolbarFrame &);
    void TimeSettingsDialog(const EditorClient &);
    uint64_t m_LogErrorsSeen = 0;
    double m_BombFlashUntil = 0;
    bool m_OpenTimeSettings = false, m_HideFps = false;
    int m_TimeFrame = 0, m_TimeBehavior = 0;
    float m_TimeFrameLimit = 60, m_TimeBehaviorLimit = 60, m_TimeScale = 1, m_TimeMin = 1, m_TimeMax = 200;
};
} // namespace vdev
