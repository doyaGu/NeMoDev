#include "ui/controls/ParameterDialog.h"
#include "workspace/EditorClient.h"

namespace vdev {
void ParameterDialog::Draw(const EditorHost *host,EditorViewFrame &frame){
        if(!open){frame.close=1;return;}
        const char *id = kind == 4   ? "Edit Parameter###AttributeValue"
                         : kind == 3 ? "Format Options###formatoptions"
                         : kind == 0 ? "Edit Parameter###AttributeManagerValue"
                         : kind == 1 ? "Edit Parameter###LevelCopyOptions"
                                     : "Select Delete Options###DeleteOptions";
        if(!shown){ImGui::OpenPopup(id);shown=true;}
        const auto action = EditParameterValueDialog(id,
                                                     kind == 3   ? "Format Options"
                                                     : kind == 2 ? "Select Delete Options"
                                                                 : "Edit Parameter",
                                                     *box, kind == 4 ? attributeTypeName.c_str() : nullptr);
        if (action == DialogAction::Cancel) {
            box->Clear();
            ClearBitmapOptions();
            open = false;
            frame.close = 1;
        } else if (action == DialogAction::Ok) {
            EditorClient client(host, view);
            client.Command("parameter.accept");
        }
        frame.active=open;
    }
} // namespace vdev
