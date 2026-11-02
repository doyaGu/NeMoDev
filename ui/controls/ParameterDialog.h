#pragma once
#include "ui/controls/ParameterBox.h"

namespace vdev {
struct ParameterDialog {

    ParameterCopies &copies;
    std::unique_ptr<ParameterBox> box;
    EditorView view=0;
    EditorScope scope{};
    ObjectRef target{}, bitmapOptions{}, attributeOwner{};
    int attributeType = -1;
    std::string attributeTypeName;
    std::vector<ObjectRef> objects;
    int kind=0;
    uint64_t bitmapRevision = 0;
    bool open=false,shown=false;

    explicit ParameterDialog(ParameterCopies &);
    ~ParameterDialog();
    void ClearBitmapOptions();
    void Clear();
    static EditorStatus Navigate(void *data,const EditorHost *host,const EditorRequest *request);
    static EditorStatus Open(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink);
    static EditorStatus Accept(void *data,const EditorHost *host,const EditorRequest *request,EditorSink sink);
    void Draw(const EditorHost *host,EditorViewFrame &frame);
};
} // namespace vdev
