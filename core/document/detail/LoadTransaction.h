#pragma once

#include <memory>
#include <string>

namespace vdev {
class Document;

// In-place checkpoint for a non-undoable load. Existing CK objects and
// ScriptSession instances retain their identities, so old undo actions stay
// usable. Keep this scope outside the protected runtime call (including SEH).
class LoadTransaction {
public:
    explicit LoadTransaction(Document &document);
    ~LoadTransaction();
    LoadTransaction(const LoadTransaction &) = delete;
    LoadTransaction &operator=(const LoadTransaction &) = delete;
    bool Capture(std::string *error);
    bool Rollback(std::string *error);
    void Commit();

private:
    struct State;
    Document &m_Document;
    std::unique_ptr<State> m_State;
    bool m_Armed = false;
};
} // namespace vdev
