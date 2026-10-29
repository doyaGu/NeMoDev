#pragma once

#include <string>

#include "CKAll.h"
#include "core/document/BehaviorDescription.h"
#include "ui/controls/BehaviorInfoFields.h"
#include "ui/controls/ParameterBox.h"

namespace vdev {

// Data Resources' Behavior Information editor. Export inputs use the same
// field form; their workflow and file output belong to the application.
class BehaviorInfoDialog {
public:
    // nms: the behavior file. name, cid: the behavior's; empty / 0: the
    // .dsc's (else the file's name, no class).
    void Open(const std::string &nms, const std::string &name, CK_CLASSID cid);
    // Ok once the .dsc is written (Written: whether it could be), Cancel.
    DialogAction Draw();
    bool Written() const { return m_Written; }
    const std::string &Path() const { return m_Path; }

private:
    bool m_Open = false, m_Written = false;
    std::string m_Path;
    BehaviorDescription m_Description;
    BehaviorInfoFields m_Fields;
};

} // namespace vdev
