#pragma once

#include "application/EditorApplication.h"
#include <functional>
#include <memory>

namespace vdev {
class Document;
class Debugger;

// Owns execution for the editing document. Native work remains on the current
// execution thread; file workers and open edits exclude these commands.
class ExecutionCommands {
  public:
    struct Settings {
        std::function<bool()> showDynamic;
        std::function<void(bool)> redrawChanged;
    };
    struct StateData {
        DocumentToken document{};
        bool available = false, trace = false, insideFrames = false, inFrame = false;
        int state = 0, frames = 0, reached = 0, maxFrames = 0, framesPerUpdate = 1;
        double simulatedFps = 60, measuredFps = 0;
        bool redrawOnlyAtBreakpoint = false;
        ObjectRef object{}, script{}, behavior{}, lockedObject{}, lockedScript{};
        uint64_t logEntries = 0, logRevision = 0, reachedBreakpoints = 0, faults = 0;
    };
    ExecutionCommands(EditorApplication &editor, std::function<Document *()> current,
                      std::function<bool()> nativeAvailable, Settings settings = {});
    ~ExecutionCommands();
    bool Register(std::string &error);
    // Application assembly/lifecycle only. The remaining native breakpoint
    // views inspect a const debugger until their view migration is complete.
    const Debugger *Bind(Document &document);
    void DocumentChanging();
    // Exclusive runtime thread only. Published state/log data stays untouched
    // until Synchronize runs on the editor thread after the replacement.
    void ReleaseDocument();
    void Synchronize();
    void Stop();
    // Returns whether runtime changes require the Schematic values to refresh.
    bool Update(double budget);
    const StateData &State() const;

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
