#pragma once

#include <functional>
#include <string>
#include <vector>

#include "application/RuntimeOperation.h"
#include "core/document/Document.h"
#include "core/document/DocumentSaver.h"
#include "core/document/LinkValidation.h"
#include "core/runtime/CkRuntimeHost.h"

namespace vdev {
class ExecutionCommands;
class SaveFileOperations;

// Application file workflows. Native stages use the exclusive runtime lane;
// UI callbacks run between submitted frames on the main thread. Results are
// borrowed for the duration of the callback. No callback for an old document
// may act on a replacement document.
class DocumentFiles {
  public:
    class Views {
      public:
        virtual ~Views() = default;
        virtual void DetachDocument(bool replaceContext) = 0;
        virtual void RestoreDocument() = 0;
        virtual bool DocumentInteractionActive() const = 0;
    };
    class Observer {
      public:
        virtual ~Observer() = default;
        // Called once after preparation succeeds, on the editor thread.
        virtual void ReplacingDocument(bool replaceContext) = 0;
        virtual void ReplacementCancelled() = 0;
        virtual void DocumentReplaced() = 0;
        virtual void Log(LogLevel level, const std::string &message) = 0;
    };
    // Supplied file operations must outlive this owner and its queued work.
    DocumentFiles(std::function<Document *()> current, RuntimeOperation &runtime, Observer &observer,
                  SaveFileOperations *operations = nullptr)
        : m_Current(std::move(current)), m_Runtime(runtime), m_Observer(observer), m_FileOperations(operations) {}
    // Application assembly. Native execution ends inside the admitted commit,
    // after candidate preparation and view detachment have succeeded.
    void Execution(ExecutionCommands *execution) { m_Execution = execution; }
    struct OpenOptions {
        SceneEnvironment environment = DefaultNewSceneEnvironment();
        bool testLinkTypes = false;
        bool removeInvalidLinks = false;
    };
    struct OpenResult {
        bool opened = false;
        LayoutInterfaceData layout;
        std::vector<InvalidLink> invalid;
        int removed = 0;
        std::string error;
    };
    // Preparation leaves the original document attached. Only this owner
    // initiates its observer transition, after the candidate is complete.
    bool Open(const std::string &path, const OpenOptions &options, std::function<void(const OpenResult &)> completed);
    // Call while idle. Replacement releases views only after admission.
    // Busy or open-Edit requests return false without changing state or
    // invoking callbacks. Only successful replacements reset the document scope.
    bool New(const SceneEnvironment &environment, std::function<void(bool)> completed);
    bool Close(std::function<void(bool)> completed);
    struct LoadResult {
        enum class Stop { Failed, Cancelled, Stale };
        bool loaded = false;
        std::vector<CK_ID> objects;
        std::string error;
        Stop stop = Stop::Failed;
    };
    void Merge(const std::string &path, bool asScene, std::function<void(const LoadResult &)> completed);
    void Import(const std::string &path, Document::ImportMode mode, const SceneEnvironment &environment,
                std::function<void(const LoadResult &)> completed);
    struct SaveResult {
        bool saved = false;
        bool clean = false; // a normal save established the current document's save point
        SaveReport report;
        bool completed = false;
    };
    bool Save(const std::string &path, const LayoutInterfaceData &layout, const SaveOptions &options,
              std::function<void(const SaveResult &)> completed);

    struct ResourceFile { std::string path; CK_CLASSID cid = 0; };
    struct ResourceLoad {
        std::vector<ResourceFile> files;
        std::vector<ObjectRef> targets;
        std::vector<ObjectRef> above;
        ObjectRef scene{};
        bool drop = false;
    };
    struct ResourceResult {
        struct Item { std::string path, message; bool loaded = false; ObjectRef created{}; };
        std::vector<Item> items;
        LoadResult::Stop stop = LoadResult::Stop::Failed;
        bool completed = false;
    };
    // One accepted batch owns its file names and receiver identities, including
    // while duplicate-name choices are shown. Each native load keeps its existing
    // undo behavior; partial batch successes remain in items.
    bool LoadResources(ResourceLoad request, std::function<void(const ResourceResult &)> completed);
    struct ExportRequest {
        std::vector<ObjectRef> objects;
        std::string path;
        bool behaviors = false, hide = false;
        bool description = false;
        std::string name, author, version, text; // UTF-8
    };
    struct ExportInfo { std::string name, className, author, version, text; };
    struct ExportResult {
        bool saved = false, completed = false;
        std::vector<std::string> written;
        std::string message;
    };
    bool DescribeExport(const ExportRequest &request, std::function<void(bool, const ExportInfo &)> completed);
    bool Export(ExportRequest request, std::function<void(const ExportResult &)> completed);
    struct WebPage {
        std::string path, title;
        int width = 320, height = 240;
        bool cmo = false, saveScriptSnapshots = true;
        LayoutInterfaceData layout;
    };
    struct WebResult : ExportResult {
        enum class Failure { None, RuntimeMetadata, Composition, Page, Publication } failure = Failure::None;
    };
    bool ExportWebPage(WebPage request, std::function<void(const WebResult &)> completed);

    // Shared with resource loads. The choices are valid only in load's call.
    using Load = std::function<void(const std::vector<DuplicateName> *)>;
    void WithDuplicates(const std::string &path, Load load, std::function<void(LoadResult::Stop)> cancelled = {});
    struct DuplicatePrompt {
        std::string path;
        std::vector<DuplicateName> duplicates;
        unsigned serial = 0;
    };
    DuplicatePrompt *PendingDuplicates();
    void ResolveDuplicates(bool accepted);
    void DocumentChanged();

  private:
    struct ResourceWork;
    void LoadNextResource(const std::shared_ptr<ResourceWork> &work);
    struct PendingLoad {
        DuplicatePrompt prompt;
        RuntimeOperation::DocumentScope document;
        Load load;
        std::function<void(LoadResult::Stop)> cancelled;
    };
    bool Run(const std::string &label, std::function<void()> work, std::function<void(bool)> completed = {});
    Document *Current() const { return m_Current ? m_Current() : nullptr; }
    bool HasDocument() const { return Current() && Current()->IsOpen(); }
    bool Replace(bool context, std::string &error);
    void Replaced();
    void Restore(std::string &error);
    std::function<Document *()> m_Current;
    RuntimeOperation &m_Runtime;
    Observer &m_Observer;
    SaveFileOperations *m_FileOperations = nullptr;
    ExecutionCommands *m_Execution = nullptr;
    std::vector<PendingLoad> m_Loads;
    unsigned m_NextPrompt = 0;
};

} // namespace vdev
