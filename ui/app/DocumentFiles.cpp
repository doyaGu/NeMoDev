#include "ui/app/DocumentFiles.h"

#include "ui/app/EditorState.h"

namespace vdev {

bool DocumentFiles::Open(const std::string &path, const OpenOptions &options,
                         std::function<void()> beforeCommit, std::function<void(const OpenResult &)> completed) {
    struct Candidate {
        std::unique_ptr<Document::PreparedDocument> prepared;
        std::string error;
        OpenResult result;
    };
    auto candidate = std::make_shared<Candidate>();
    return m_State.RunRuntime("Opening composition", [this, path, options, candidate] {
        candidate->prepared = m_State.document->PrepareOpen(path, &candidate->error, options.environment);
    }, [this, options, candidate, beforeCommit = std::move(beforeCommit), completed = std::move(completed)](bool ok) {
        if (!ok || !candidate->prepared) {
            if (!candidate->error.empty()) m_State.Log(LogLevel::Error, candidate->error);
            if (completed) completed(candidate->result);
            return;
        }
        // The UI transition deliberately changes the scope. Its commit is
        // queued in the new scope; queued requests for the old one expire.
        if (beforeCommit) beforeCommit();
        m_State.ClearDocumentState();
        m_State.RunRuntime("Finishing composition load", [this, options, candidate] {
            candidate->prepared->Commit();
            candidate->prepared.reset();
            Document &document = *m_State.document;
            auto &result = candidate->result;
            if (document.CreatedFrom() == Document::Origin::LoadedComposition) {
                result.layout = ReadLayoutInterfaceData(document.Context());
                result.invalid = FindInvalidLinks(document, options.testLinkTypes);
                if (options.removeInvalidLinks && !result.invalid.empty()) {
                    result.removed = RemoveInvalidLinks(document, result.invalid);
                    result.invalid.clear();
                }
            }
            if (!document.Scripts().empty()) document.Session(document.Scripts().front().id);
            document.History().MarkSavePoint(); // NMO remains a modified New + Import.
            result.opened = true;
        }, [candidate, completed](bool committed) {
            candidate->result.opened = committed && candidate->result.opened;
            if (completed) completed(candidate->result);
        });
    });
}

bool DocumentFiles::New(const SceneEnvironment &environment, std::function<void(bool)> completed) {
    if (m_State.runtimeOperation.Busy() || !m_State.document || m_State.document->EditOpen()) return false;
    m_State.ClearDocumentState();
    struct Result { bool created = false; std::string error; };
    auto result = std::make_shared<Result>();
    return m_State.RunRuntime("Creating composition", [this, result, environment] {
        result->created = m_State.document->New(&result->error, environment);
        if (result->created) m_State.History().MarkSavePoint();
    }, [this, result, completed = std::move(completed)](bool ok) {
        if (!result->error.empty()) m_State.Log(LogLevel::Error, result->error);
        if (completed) completed(ok && result->created);
    });
}

bool DocumentFiles::Close(std::function<void(bool)> completed) {
    if (m_State.runtimeOperation.Busy() || !m_State.document || m_State.document->EditOpen()) return false;
    m_State.ClearDocumentState();
    return m_State.RunRuntime("Closing composition", [this] { m_State.document->Close(); }, std::move(completed));
}

void DocumentFiles::Merge(const std::string &path, bool asScene, std::function<void(const LoadResult &)> completed) {
    WithDuplicates(path, [this, path, asScene, completed](const std::vector<DuplicateName> *duplicates) {
        auto result = std::make_shared<LoadResult>();
        auto choices = duplicates ? std::make_shared<std::vector<DuplicateName>>(*duplicates) : nullptr;
        m_State.RunRuntime("Merging composition", [this, path, asScene, choices, result] {
            result->loaded = m_State.document->Merge(path, asScene, &result->error, &result->objects, choices.get());
        }, [this, result, completed](bool ok) {
            result->loaded = ok && result->loaded;
            if (!result->error.empty()) m_State.Log(LogLevel::Error, result->error);
            if (completed) completed(*result);
        });
    }, [completed] { if (completed) completed({}); });
}

void DocumentFiles::Import(const std::string &path, Document::ImportMode mode, const SceneEnvironment &environment,
                           std::function<void(const LoadResult &)> completed) {
    WithDuplicates(path, [this, path, mode, environment, completed](const std::vector<DuplicateName> *duplicates) {
        auto result = std::make_shared<LoadResult>();
        auto choices = duplicates ? std::make_shared<std::vector<DuplicateName>>(*duplicates) : nullptr;
        m_State.RunRuntime("Importing file", [this, path, mode, environment, choices, result] {
            result->loaded = m_State.document->Import(path, mode, &result->error, &result->objects, choices.get(), environment);
        }, [this, result, completed](bool ok) {
            result->loaded = ok && result->loaded;
            if (!result->error.empty()) m_State.Log(LogLevel::Error, result->error);
            if (completed) completed(*result);
        });
    }, [completed] { if (completed) completed({}); });
}

bool DocumentFiles::Save(const std::string &path, const LayoutInterfaceData &layout, const SaveOptions &options,
                         std::function<void(const SaveResult &)> completed) {
    auto result = std::make_shared<SaveResult>();
    return m_State.RunRuntime(options.markSaved ? "Saving composition" : "Exporting composition", [this, path, options, layout, result] {
        result->saved = SaveComposition(*m_State.document, path, {&layout}, options, result->report);
        result->clean = result->report.documentSaved && !m_State.document->Dirty();
    }, [result, completed = std::move(completed)](bool ok) {
        result->saved = ok && result->saved;
        result->clean = ok && result->clean;
        if (completed) completed(*result);
    });
}

void DocumentFiles::WithDuplicates(const std::string &path, Load load, std::function<void()> cancelled) {
    auto found = std::make_shared<std::vector<DuplicateName>>();
    m_State.RunRuntime("Checking duplicate names", [this, path, found] {
        if (m_State.HasDocument()) *found = m_State.document->FindDuplicateNames(path);
    }, [this, path, found, load = std::move(load), cancelled = std::move(cancelled)](bool ok) mutable {
        if (!ok) { if (cancelled) cancelled(); return; }
        if (found->empty()) { if (load) load(nullptr); }
        else m_Loads.push_back({{path, std::move(*found), ++m_NextPrompt}, m_State.runtimeOperation.CurrentDocument(),
                               std::move(load), std::move(cancelled)});
    });
}

DocumentFiles::DuplicatePrompt *DocumentFiles::PendingDuplicates() {
    while (!m_Loads.empty() && !m_Loads.front().document.IsCurrent()) m_Loads.erase(m_Loads.begin());
    return m_Loads.empty() ? nullptr : &m_Loads.front().prompt;
}

void DocumentFiles::ResolveDuplicates(bool accepted) {
    if (!PendingDuplicates()) return;
    PendingLoad done = std::move(m_Loads.front());
    m_Loads.erase(m_Loads.begin());
    if (accepted && done.load) done.load(&done.prompt.duplicates);
    else {
        m_State.Log(LogLevel::Info, "Load cancelled: " + done.prompt.path);
        if (done.cancelled) done.cancelled();
    }
}

void DocumentFiles::DocumentChanged() { m_Loads.clear(); }

} // namespace vdev
