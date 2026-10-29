#pragma once

#include "application/DocumentFiles.h"
#include "application/EditorApplication.h"

namespace vdev {

// Application-owned file operations. Views present requests and results;
// they neither choose the next step nor keep the continuation alive.
class DocumentCommands {
  public:
    enum class Action {
        New,
        Open,
        Save,
        SaveAs,
        SaveVersion,
        Revert,
        Close,
        Quit,
        Merge,
        MergeScene,
        Import,
        ImportCharacter,
        ImportPlace,
        ImportScene,
        Player,
        LoadResources,
        DropResources,
        LoadScripts,
        ExportObjects,
        ExportBehaviors,
        Undo,
        Redo,
        Purge
    };
    struct Settings {
        DocumentFiles::OpenOptions open;
        bool saveScriptSnapshots = true;
    };
    struct Input {
        enum class Kind {
            File = EDITOR_FILE_INPUT,
            Unsaved = EDITOR_SAVE_CHANGES_INPUT,
            Confirm = EDITOR_CONFIRM_INPUT,
            ObjectExport = EDITOR_OBJECT_EXPORT_INPUT,
            BehaviorExport = EDITOR_BEHAVIOR_EXPORT_INPUT
        };
        enum class File { Composition, Import, Player, Objects, Behaviors };
        uint64_t id = 0;
        EditorOperation operation = 0;
        Kind kind = Kind::File;
        File file = File::Composition;
        bool save = false;
        std::string title, text, initial;
        Action action = Action::Open;
        std::string extension;
        std::vector<std::pair<std::string, std::string>> filters;
        uint64_t objects = 0;
        bool hide = false;
        DocumentFiles::ExportInfo behavior;
    };
    struct Result {
        EditorOperation operation = 0;
        Action action = Action::Open;
        EditorStatus status = EDITOR_QUEUED;
        DocumentToken document{};
        std::string path;
        std::string message;
        std::vector<std::string> written;
        DocumentFiles::OpenResult opened;
        DocumentFiles::LoadResult loaded;
        DocumentFiles::SaveResult saved;
        DocumentFiles::ResourceResult resources;
        DocumentFiles::ExportResult exported;
    };
    class Presentation {
      public:
        virtual ~Presentation() = default;
        virtual LayoutInterfaceData CaptureLayout() const = 0;
        virtual void FileFinished(const Result &result) = 0;
        virtual void HistoryChanged() = 0;
        virtual void Quit() = 0;
    };
    DocumentCommands(EditorApplication &editor, DocumentFiles &files, RuntimeOperation &runtime,
                     std::function<Document *()> current, Presentation &presentation);
    ~DocumentCommands();
    bool Register(std::string &error);
    void Configure(Settings settings);
    bool Busy() const;
    const Input *PendingInput() const;
    EditorStatus Respond(uint64_t input, const std::string &choice, const std::string &path = {});
    std::string CurrentFilePath() const;
    void Poll();
    void Stop();

  private:
    struct State;
    std::unique_ptr<State> m_State;
};

} // namespace vdev
