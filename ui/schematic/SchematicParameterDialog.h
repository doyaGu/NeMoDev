#pragma once
#include "workspace/EditorClient.h"
#include <memory>

namespace vdev {
// A parameter dialog owns copied fields and one pending command. Working
// parameters and applying their values belong to the command provider.
class SchematicParameterDialog {
  public:
    SchematicParameterDialog(EditorApplication &, const EditorHost *, EditorView);
    ~SchematicParameterDialog();
    bool Open(ObjectRef script, ObjectRef object, unsigned mode);
    void Draw();
    void Close();
    std::string TakeMessage();

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
