#pragma once

#include <string>

#include "CKAll.h"

namespace vdev {

// Why an edit was not applied: nothing to do (the value is the same, not an
// error), refused (the edit is not possible here), or the runtime raised an
// exception (GuardedCall).
enum class EditStatus { Applied, NoChange, Refused, Error };

// What an edit did: ok when it was applied (one undo step unless the
// command says otherwise); otherwise message tells why and status how. The
// message uses the runtime's CK encoding, as the names it may contain
// (the editor shows it with AnsiToUtf8); UTF-8 paths stay in file-operation
// inputs and runtime log entries. Callers tell its
// kinds apart by status, not by its text. Made by Applied, NoChange, Refused
// or Error only (a default one was refused without saying why); ok and
// status are theirs to set, a command may change the message.
struct EditResult {
    bool ok = false;
    std::string message; // why the edit was refused, or a note (CK encoding)
    CK_ID created = 0;   // new behavior, link, local parameter, operation or object
    EditStatus status = EditStatus::Refused; // when not ok

    EditStatus Status() const { return ok ? EditStatus::Applied : status; }
    // Nothing was done because nothing was to be done: no warning is due.
    bool Unchanged() const { return !ok && status == EditStatus::NoChange; }

    static EditResult Applied(const std::string &message = std::string(), CK_ID created = 0) {
        EditResult result;
        result.ok = true;
        result.status = EditStatus::Applied;
        result.message = message;
        result.created = created;
        return result;
    }
    static EditResult NoChange(const std::string &message = "No change") { return Failure(EditStatus::NoChange, message); }
    static EditResult Refused(const std::string &message) { return Failure(EditStatus::Refused, message); }
    static EditResult Error(const std::string &message) { return Failure(EditStatus::Error, message); }

private:
    EditResult() = default;
    static EditResult Failure(EditStatus status, const std::string &message) {
        EditResult result;
        result.status = status;
        result.message = message;
        return result;
    }
};

} // namespace vdev
