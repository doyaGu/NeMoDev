#pragma once
#include "application/EditorApplication.h"
#include "core/graph/ParameterValueData.h"
#include <functional>
#include <memory>

namespace vdev {
class CkRuntimeHost;
class EditorWorkspace;
class ParameterBox;
struct ParameterDependencies {
    uint32_t flags = 0;
    std::vector<uint32_t> options;
};
// Owns access to native working parameters. Widgets receive identities and
// copied values; only temporary parameters can be written by Restore/WriteText.
class ParameterCopies {
  public:
    ParameterCopies(CkRuntimeHost &, EditorWorkspace &, std::function<Document *()>,
                    std::function<ParameterDependencies()> copyOptions,
                    std::function<void()> clearSelection);
    ~ParameterCopies();
    EditorApplication &Editor() const;
    bool Read(ObjectRef, ParameterValue &) const;
    bool Parse(ObjectRef, const EditorValue &, ParameterValue &) const;
    bool Restore(ObjectRef, const ParameterValue &);
    bool Current(ObjectRef) const;
    std::string ReadText(ObjectRef) const;
    bool WriteText(ObjectRef, const std::string &);
    std::vector<std::pair<int32_t, std::string>> Classes(int32_t) const;
    ObjectRef Pick(float x, float y, int32_t objectClass) const;

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
    friend class ParameterBox;
    friend struct ParameterDialog;
};
bool RegisterParameterDialogs(EditorApplication &, ParameterCopies &, std::string &error);
} // namespace vdev
