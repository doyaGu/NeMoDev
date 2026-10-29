#pragma once

#include "application/DocumentFiles.h"
#include "application/EditorApplication.h"
#include "application/Printing.h"

namespace vdev {

// Document outputs and their pending input belong to the application. Views
// only show copied values and answer input IDs through registered commands.
class OutputCommands {
  public:
    struct Result {
        EditorOperation operation = 0;
        EditorStatus status = EDITOR_QUEUED;
        bool print = false;
        DocumentFiles::WebResult::Failure webFailure = DocumentFiles::WebResult::Failure::None;
        std::string path, message, printerJob;
        std::vector<std::string> written;
    };
    class Presentation {
      public:
        virtual ~Presentation() = default;
        virtual LayoutInterfaceData CaptureLayout() const = 0;
        virtual std::unique_ptr<SchematicPrintContent> CapturePrint(std::string &error) = 0;
        virtual PrintChoice ChoosePrinter(const std::string &directory) = 0;
        virtual bool OpenOutput(const std::string &path, std::string &error) = 0;
        virtual void OutputFinished(const Result &result) = 0;
    };
    OutputCommands(EditorApplication &editor, DocumentFiles &files, RuntimeOperation &runtime,
                   std::function<Document *()> current, Presentation &presentation);
    ~OutputCommands();
    bool Register(std::string &error);
    void SaveScriptSnapshots(bool enabled);
    bool Busy() const;
    void Poll();
    void Stop();

  private:
    struct Data;
    std::unique_ptr<Data> m_Data;
};
} // namespace vdev
