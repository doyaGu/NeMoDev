#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/document/Document.h"
#include "core/document/DocumentSaver.h"
#include "core/document/LinkValidation.h"
#include "ui/app/RuntimeOperation.h"

namespace vdev {

struct EditorState;

// Application file workflows. Native stages use the exclusive runtime lane;
// UI callbacks run between submitted frames on the main thread. Results are
// borrowed for the duration of the callback. No callback for an old document
// may act on a replacement document.
class DocumentFiles {
public:
    explicit DocumentFiles(EditorState &state) : m_State(state) {}
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
    };
    // beforeCommit releases old windows/render state only after a candidate
    // has loaded successfully. This module then resets the document's UI session.
    bool Open(const std::string &path, const OpenOptions &options,
              std::function<void()> beforeCommit, std::function<void(const OpenResult &)> completed);
    // Call while idle, after detaching the old document's UI/render state.
    // Busy or open-Edit requests return false without changing state or
    // invoking callbacks. Accepted replacements reset the document scope.
    bool New(const SceneEnvironment &environment, std::function<void(bool)> completed);
    bool Close(std::function<void(bool)> completed);
    struct LoadResult {
        bool loaded = false;
        std::vector<CK_ID> objects;
        std::string error;
    };
    void Merge(const std::string &path, bool asScene, std::function<void(const LoadResult &)> completed);
    void Import(const std::string &path, Document::ImportMode mode, const SceneEnvironment &environment,
                std::function<void(const LoadResult &)> completed);
    struct SaveResult {
        bool saved = false;
        bool clean = false; // a normal save established the current document's save point
        SaveReport report;
    };
    bool Save(const std::string &path, const LayoutInterfaceData &layout, const SaveOptions &options,
              std::function<void(const SaveResult &)> completed);

    // Shared with resource loads. The choices are valid only in load's call.
    using Load = std::function<void(const std::vector<DuplicateName> *)>;
    void WithDuplicates(const std::string &path, Load load, std::function<void()> cancelled = {});
    struct DuplicatePrompt {
        std::string path;
        std::vector<DuplicateName> duplicates;
        unsigned serial = 0;
    };
    DuplicatePrompt *PendingDuplicates();
    void ResolveDuplicates(bool accepted);
    void DocumentChanged();

private:
    struct PendingLoad {
        DuplicatePrompt prompt;
        RuntimeOperation::DocumentScope document;
        Load load;
        std::function<void()> cancelled;
    };
    EditorState &m_State;
    std::vector<PendingLoad> m_Loads;
    unsigned m_NextPrompt = 0;
};

} // namespace vdev
