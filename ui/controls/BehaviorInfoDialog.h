#pragma once

#include <string>

#include "CKAll.h"
#include "core/document/ToolCommands.h"
#include "ui/controls/ParameterBox.h"

namespace vdev {

// Dev's Behavior Information (dialog 589, Dev.exe sub_581FB0, sub_582140):
// a behavior file's description (its .dsc, BehaviorDescription). Asked
// when behaviors are saved (the first one's name and class; Dev saves with
// OK only) and from a behavior file of a Data Resource (the .dsc's). The
// behavior and the class it applies to are shown; the Author, the Version
// and the Description are edited; OK writes the .dsc.
class BehaviorInfoDialog {
public:
    // nms: the behavior file. name, cid: the behavior's; empty / 0: the
    // .dsc's (else the file's name, no class).
    // saving: Dev's save of behaviors, with "Hide script representation in
    // schematic view" (Dev.exe dialog 709).
    void Open(const std::string &nms, const std::string &name, CK_CLASSID cid, bool saving = false);
    bool HideRepresentation() const { return m_Saving && m_Hide; }
    // Ok once the .dsc is written (Written: whether it could be), Cancel.
    DialogAction Draw();
    bool Written() const { return m_Written; }
    const std::string &Path() const { return m_Path; }

private:
    bool m_Open = false, m_Written = false, m_Saving = false, m_Hide = false;
    std::string m_Path;
    BehaviorDescription m_Description;
    std::string m_Name, m_Author, m_Version, m_Text; // UTF-8, as displayed/edited
};

} // namespace vdev
