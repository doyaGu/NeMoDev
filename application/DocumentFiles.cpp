#include "application/DocumentFiles.h"
#include "application/ExecutionCommands.h"
#include "core/document/ToolCommands.h"
#include "core/document/ExportCommands.h"
#include "core/document/FileCommands.h"
#include "core/document/OutputFiles.h"
#include "core/graph/NmsCommands.h"
#include "core/graph/ReplaceCommands.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/ScriptSession.h"
#include <fstream>
#include "core/runtime/TextEncoding.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace vdev {

bool DocumentFiles::Run(const std::string &label, std::function<void()> work, std::function<void(bool)> completed) {
    return m_Runtime.Queue(
        label, std::move(work),
        [this, completed = std::move(completed)](const std::string &error) {
            if (!error.empty()) m_Observer.Log(LogLevel::Error, error);
            if (completed) completed(error.empty());
        },
        RuntimeOperation::Delivery::Always);
}

bool DocumentFiles::Replace(bool context, std::string &error) {
    try {
        m_Observer.ReplacingDocument(context);
        return true;
    } catch (const std::exception &failure) {
        error = std::string("Cannot detach document views: ") + failure.what();
    } catch (...) {
        error = "Cannot detach document views";
    }
    Restore(error);
    return false;
}

void DocumentFiles::Restore(std::string &error) {
    try {
        m_Observer.ReplacementCancelled();
    } catch (const std::exception &failure) {
        error += std::string("; cannot restore document views: ") + failure.what();
    } catch (...) {
        error += "; cannot restore document views";
    }
    if (!error.empty()) m_Observer.Log(LogLevel::Error, error);
}

void DocumentFiles::Replaced() {
    // Old queued work expires only after an actual ownership change. A failed
    // detach/admission must leave the old session, selection and history intact.
    m_Runtime.DocumentChanged();
    DocumentChanged();
    m_Observer.DocumentReplaced();
}

bool DocumentFiles::Open(const std::string &path, const OpenOptions &options,
                         std::function<void(const OpenResult &)> completed) {
    struct Candidate {
        std::unique_ptr<Document::PreparedDocument> prepared;
        OpenResult result;
    };
    auto candidate = std::make_shared<Candidate>();
    const auto scope = m_Runtime.CurrentDocument();
    return Run(
        "Opening composition",
        [this, path, options, candidate] {
            auto &result = candidate->result;
            candidate->prepared = Current()->PrepareOpen(path, &result.error, options.environment);
            if (!candidate->prepared) return;
            auto &document = candidate->prepared->Candidate();
            // Complete potentially failing native preparation on the candidate,
            // while the original document and its views are still attached.
            if (document.CreatedFrom() == Document::Origin::LoadedComposition) {
                result.layout = ReadLayoutInterfaceData(document.Context());
                result.invalid = FindInvalidLinks(document, options.testLinkTypes);
                if (options.removeInvalidLinks && !result.invalid.empty()) {
                    result.removed = RemoveInvalidLinks(document, result.invalid);
                    result.invalid.clear();
                }
            }
            if (!document.Scripts().empty()) document.Session(document.Scripts().front().id);
            document.History().MarkSavePoint();
        },
        [this, candidate, scope, completed = std::move(completed)](bool ok) {
            auto &result = candidate->result;
            if (!ok || !candidate->prepared || !scope.IsCurrent()) {
                if (!scope.IsCurrent()) result.error = "The document changed before opening completed";
                else if (result.error.empty()) result.error = "Opening composition failed";
                if (completed) completed(result);
                return;
            }
            if (!Replace(true, result.error)) {
                if (completed) completed(result);
                return;
            }
            Run("Finishing composition load", [this, candidate] {
                if (m_Execution) m_Execution->ReleaseDocument();
                candidate->prepared->Commit();
                candidate->result.opened = true;
                candidate->prepared.reset();
            }, [this, candidate, completed](bool committed) {
                auto &result = candidate->result;
                if (result.opened) {
                    Replaced();
                    if (!committed) result.error = "Composition loaded, but finishing the runtime operation failed";
                } else {
                    result.error = "Composition was not switched";
                    Restore(result.error);
                }
                if (completed) completed(result);
            });
        });
}

bool DocumentFiles::New(const SceneEnvironment &environment, std::function<void(bool)> completed) {
    if (m_Runtime.Busy() || !Current() || Current()->EditOpen() || !m_Runtime.CanQueue("Creating composition"))
        return false;
    struct Candidate {
        std::unique_ptr<Document::PreparedDocument> prepared;
        std::string error;
        bool committed = false;
    };
    auto candidate = std::make_shared<Candidate>();
    const auto scope = m_Runtime.CurrentDocument();
    return Run("Creating composition", [this, candidate, environment] {
        candidate->prepared = Current()->PrepareNew(&candidate->error, environment);
        if (candidate->prepared) candidate->prepared->Candidate().History().MarkSavePoint();
    }, [this, candidate, scope, completed = std::move(completed)](bool ok) {
        if (!ok || !candidate->prepared || !scope.IsCurrent() || !Replace(true, candidate->error)) {
            if (!candidate->error.empty()) m_Observer.Log(LogLevel::Error, candidate->error);
            if (completed) completed(false);
            return;
        }
        Run("Finishing new composition", [this, candidate] {
            if (m_Execution) m_Execution->ReleaseDocument();
            candidate->prepared->Commit();
            candidate->committed = true;
            candidate->prepared.reset();
        }, [this, candidate, completed](bool) {
            if (candidate->committed) Replaced();
            else { candidate->error = "New composition was not switched"; Restore(candidate->error); }
            if (completed) completed(candidate->committed);
        });
    });
}

bool DocumentFiles::Close(std::function<void(bool)> completed) {
    if (m_Runtime.Busy() || !Current() || Current()->EditOpen() || !m_Runtime.CanQueue("Closing composition"))
        return false;
    std::string error;
    if (!Replace(false, error)) { if (completed) completed(false); return true; }
    auto closed = std::make_shared<bool>(false);
    return Run("Closing composition", [this, closed] {
                   if (m_Execution) m_Execution->ReleaseDocument();
                   Current()->Close(); *closed = true;
               },
               [this, closed, completed = std::move(completed)](bool) {
                   if (*closed) Replaced();
                   else { std::string error = "Composition was not closed"; Restore(error); }
                   if (completed) completed(*closed);
               });
}

void DocumentFiles::Merge(const std::string &path, bool asScene, std::function<void(const LoadResult &)> completed) {
    WithDuplicates(
        path,
        [this, path, asScene, completed](const std::vector<DuplicateName> *duplicates) {
            auto result = std::make_shared<LoadResult>();
            auto choices = duplicates ? std::make_shared<std::vector<DuplicateName>>(*duplicates) : nullptr;
            Run(
                "Merging composition",
                [this, path, asScene, choices, result] {
                    result->loaded = Current()->Merge(path, asScene, &result->error, &result->objects, choices.get());
                },
                [this, result, completed](bool ok) {
                    result->loaded = ok && result->loaded;
                    if (!result->error.empty()) m_Observer.Log(LogLevel::Error, result->error);
                    if (completed) completed(*result);
                });
        },
        [completed](LoadResult::Stop stop) {
            if (completed) {
                LoadResult result;
                result.stop = stop;
                completed(result);
            }
        });
}

void DocumentFiles::Import(const std::string &path, Document::ImportMode mode, const SceneEnvironment &environment,
                           std::function<void(const LoadResult &)> completed) {
    WithDuplicates(
        path,
        [this, path, mode, environment, completed](const std::vector<DuplicateName> *duplicates) {
            auto result = std::make_shared<LoadResult>();
            auto choices = duplicates ? std::make_shared<std::vector<DuplicateName>>(*duplicates) : nullptr;
            Run(
                "Importing file",
                [this, path, mode, environment, choices, result] {
                    result->loaded =
                        Current()->Import(path, mode, &result->error, &result->objects, choices.get(), environment);
                },
                [this, result, completed](bool ok) {
                    result->loaded = ok && result->loaded;
                    if (!result->error.empty()) m_Observer.Log(LogLevel::Error, result->error);
                    if (completed) completed(*result);
                });
        },
        [completed](LoadResult::Stop stop) {
            if (completed) {
                LoadResult result;
                result.stop = stop;
                completed(result);
            }
        });
}

bool DocumentFiles::Save(const std::string &path, const LayoutInterfaceData &layout, const SaveOptions &options,
                         std::function<void(const SaveResult &)> completed) {
    auto result = std::make_shared<SaveResult>();
    return Run(
        options.markSaved ? "Saving composition" : "Exporting composition",
        [this, path, options, layout, result] {
            result->saved = SaveComposition(*Current(), path, {&layout}, options, result->report);
            result->clean = result->report.documentSaved && !Current()->Dirty();
        },
        [result, completed = std::move(completed)](bool ok) {
            result->saved = result->report.saved;
            result->clean = ok && result->clean;
            result->completed = ok;
            if (completed) completed(*result);
        });
}

bool DocumentFiles::ExportWebPage(WebPage request, std::function<void(const WebResult &)> completed) {
    if (!HasDocument() || request.path.empty() || request.width <= 0 || request.height <= 0) return false;
    auto result = std::make_shared<WebResult>();
    return Run("Creating web page", [this, request = std::move(request), result] {
        int major = 0, minor = 0;
        if (!RuntimeVersion(Current()->Host().RuntimeDirectory(), major, minor)) {
            result->failure = WebResult::Failure::RuntimeMetadata;
            result->message = "Cannot create web page: invalid runtime metadata";
            return;
        }
        const auto composition = WebPageCompositionPath(request.path, request.cmo);
        std::error_code error;
        if (!Current()->SourcePath().empty() && std::filesystem::equivalent(std::filesystem::u8path(composition),
                std::filesystem::u8path(Current()->SourcePath()), error)) {
            result->message = "The loaded source cannot be overwritten";
            result->failure = WebResult::Failure::Composition;
            return;
        }
        SaveReport report;
        {
            OutputFiles outputs(report, m_FileOperations);
            const auto stagedComposition = outputs.Add(composition), stagedPage = outputs.Add(request.path);
            result->failure = stagedComposition.empty() ? WebResult::Failure::Composition : WebResult::Failure::Page;
            if (!stagedComposition.empty() && !stagedPage.empty()) {
                SaveOptions options;
                options.allowOverwrite = true;
                options.markSaved = false;
                options.saveScriptSnapshots = request.saveScriptSnapshots;
                options.writeMode = CKFILE_WHOLECOMPRESSED | (request.cmo ? 0u : CKFILE_FORVIEWER);
                result->failure = WebResult::Failure::Composition;
                if (SaveComposition(*Current(), stagedComposition, {&request.layout}, options, report)) {
                    result->failure = WebResult::Failure::Page;
                    // The old template uses the runtime's native text encoding.
                    const auto html = WebPageHtml(Utf8ToAnsi(request.title), composition, request.width, request.height, major, minor);
                    std::ofstream page(std::filesystem::u8path(stagedPage), std::ios::binary | std::ios::trunc);
                    page.write(html.data(), static_cast<std::streamsize>(html.size()));
                    page.close();
                    std::ifstream verify(std::filesystem::u8path(stagedPage), std::ios::binary);
                    const std::string actual{std::istreambuf_iterator<char>(verify), {}};
                    if (!page || !verify || actual != html) report.errors.push_back("Cannot write and verify the web page");
                    else {
                        result->failure = WebResult::Failure::Publication;
                        result->saved = outputs.Publish(result->written);
                        if (result->saved) result->failure = WebResult::Failure::None;
                    }
                }
            }
        }
        for (const auto &text : report.errors) { if (!result->message.empty()) result->message += "; "; result->message += AnsiToUtf8(text); }
        for (const auto &text : report.notes) { if (!result->message.empty()) result->message += "; "; result->message += AnsiToUtf8(text); }
    }, [result, completed = std::move(completed)](bool ok) {
        result->completed = ok;
        if (completed) completed(*result);
    });
}

struct DocumentFiles::ResourceWork {
    ResourceLoad request;
    ResourceResult result;
    RuntimeOperation::DocumentScope scope;
    std::function<void(const ResourceResult &)> completed;
    size_t index = 0;
};

bool DocumentFiles::LoadResources(ResourceLoad request, std::function<void(const ResourceResult &)> completed) {
    if (!HasDocument() || request.files.empty()) return false;
    for (const auto &file : request.files)
        if (file.path.empty() || file.cid < 0 || file.cid >= CKGetClassCount()) return false;
    if (request.targets.empty()) request.targets.push_back({});
    auto work = std::make_shared<ResourceWork>();
    work->request = std::move(request);
    work->scope = m_Runtime.CurrentDocument();
    work->completed = std::move(completed);
    LoadNextResource(work);
    return true;
}

void DocumentFiles::LoadNextResource(const std::shared_ptr<ResourceWork> &work) {
    const auto &request = work->request;
    const auto valid = [&] {
        if (!work->scope.IsCurrent() || !HasDocument()) return false;
        for (auto target : request.targets) if (target && !Current()->Resolve(target)) return false;
        for (auto above : request.above) if (!Current()->Resolve(above)) return false;
        return !request.scene || Current()->Resolve(request.scene, CKCID_SCENE);
    };
    if (!valid()) {
        work->result.stop = LoadResult::Stop::Stale;
        if (work->completed) work->completed(work->result);
        return;
    }
    if (work->index == request.files.size() * request.targets.size()) {
        work->result.completed = true;
        if (work->completed) work->completed(work->result);
        return;
    }
    const auto file = request.files[work->index / request.targets.size()];
    const auto target = request.targets[work->index % request.targets.size()];
    auto load = [this, work, file, target](const std::vector<DuplicateName> *duplicates) {
        auto choices = duplicates ? std::make_shared<std::vector<DuplicateName>>(*duplicates) : nullptr;
        auto item = std::make_shared<ResourceResult::Item>();
        item->path = file.path;
        auto stale = std::make_shared<bool>(false);
        Run("Loading resource", [this, work, file, target, choices, item, stale] {
            const auto &r = work->request;
            if (!work->scope.IsCurrent() || !HasDocument() || (target && !Current()->Resolve(target)) ||
                (r.scene && !Current()->Resolve(r.scene, CKCID_SCENE))) { *stale = true; return; }
            LevelDropTarget placement;
            placement.row = target.object;
            placement.scene = r.scene.object;
            for (auto above : r.above) {
                if (!Current()->Resolve(above)) { *stale = true; return; }
                placement.above.push_back(above.object);
            }
            const auto result = r.drop ? DropResourceFile(*Current(), file.path, file.cid, placement, choices.get())
                              : LoadResourceFile(*Current(), file.path, file.cid, target.object, nullptr, choices.get());
            item->loaded = result.ok;
            item->message = AnsiToUtf8(result.message);
            if (result.ok) item->created = Current()->Reference(result.created);
        }, [this, work, item, stale](bool ok) {
            // Record an already completed load even if the executor failed after
            // returning from it. Never continue native work in that case.
            work->result.items.push_back(*item);
            if (!ok || *stale || !work->scope.IsCurrent()) {
                work->result.stop = *stale || !work->scope.IsCurrent() ? LoadResult::Stop::Stale : LoadResult::Stop::Failed;
                if (work->completed) work->completed(work->result);
                return;
            }
            ++work->index;
            LoadNextResource(work);
        });
    };
    if (!(request.drop && file.cid == 0) && ResourceFileIsImported(file.cid))
        WithDuplicates(file.path, std::move(load), [work](LoadResult::Stop stop) {
            work->result.stop = stop;
            if (work->completed) work->completed(work->result);
        });
    else
        load(nullptr);
}

bool DocumentFiles::DescribeExport(const ExportRequest &request,
                                   std::function<void(bool, const ExportInfo &)> completed) {
    auto info = std::make_shared<ExportInfo>();
    auto valid = std::make_shared<bool>(false);
    return Run("Reading behavior description", [this, request, info, valid] {
        if (!HasDocument() || request.objects.empty()) return;
        for (auto ref : request.objects) if (!Current()->Resolve(ref, CKCID_BEHAVIOR)) return;
        auto *first = CKBehavior::Cast(Current()->Resolve(request.objects.front()));
        BehaviorDescription description;
        const bool read = ReadBehaviorDescription(request.path, description);
        if (first->GetName() && *first->GetName()) description.name = first->GetName();
        description.cid = first->GetCompatibleClassID();
        info->name = description.name.empty() ? std::filesystem::u8path(request.path).stem().u8string() : AnsiToUtf8(description.name);
        const auto *className = CKClassIDToString(description.cid);
        info->className = className ? AnsiToUtf8(className) : "";
        info->author = AnsiToUtf8(description.author);
        info->text = AnsiToUtf8(description.description);
        if (read) {
            char version[64];
            std::snprintf(version, sizeof(version), "%g", description.version);
            info->version = version;
        }
        *valid = true;
    }, [info, valid, completed = std::move(completed)](bool ok) {
        if (completed) completed(ok && *valid, *info);
    });
}

bool DocumentFiles::EditGraph(GraphRequest request, std::function<void(const GraphResult &)> completed) {
    auto result = std::make_shared<GraphResult>();
    const auto scope = m_Runtime.CurrentDocument();
    return Run(
        request.replace ? "Replacing behavior graph" : "Importing behavior graph",
        [this, request, result, scope] {
            auto *doc = Current();
            auto *script = doc ? CKBehavior::Cast(doc->Resolve(request.script)) : nullptr;
            auto *graph = doc ? CKBehavior::Cast(doc->Resolve(request.graph)) : nullptr;
            auto *session = script && doc->FindScript(script->GetID()) ? doc->Session(script->GetID()) : nullptr;
            if (!scope.IsCurrent() || !graph || !session || (graph != script && OwningScript(graph) != script) ||
                session->Revision() != request.revision || doc->TopologyRevision() != request.topology) {
                result->stop = LoadResult::Stop::Stale;
                result->edit = EditResult::Refused("The target graph changed before loading");
                return;
            }
            if (session->ReadOnly()) {
                result->edit = EditResult::Refused("The Script is read-only");
                return;
            }
            result->edit = request.replace ? ReplaceWithFileGraph(*doc, script->GetID(), graph->GetID(), request.path)
                                           : ImportBehaviorGraph(*doc, script->GetID(), graph->GetID(), request.path,
                                                                 Point(request.h, request.v));
            result->created = doc->Reference(result->edit.created);
            if (auto *created = CKBehavior::Cast(doc->Resolve(result->created)))
                result->owner = doc->Reference(created->GetParent() ? created->GetParent()->GetID() : 0);
            result->documentRevision = doc->ChangeRevision();
            result->completed = true;
        },
        [result, scope, completed = std::move(completed)](bool ok) {
            const bool nativeCompleted = result->completed;
            result->completed = nativeCompleted || ok;
            if (!scope.IsCurrent())
                result->stop = LoadResult::Stop::Stale;
            if (!ok && !nativeCompleted)
                result->edit = EditResult::Error("The graph file operation failed");
            else if (!ok)
                result->edit.message +=
                    (result->edit.message.empty() ? "" : "; ") +
                    std::string("The graph operation completed before the runtime reported a failure");
            if (completed)
                completed(*result);
        });
}

bool DocumentFiles::Export(ExportRequest request, std::function<void(const ExportResult &)> completed) {
    auto result = std::make_shared<ExportResult>();
    auto native = std::make_shared<NativeExportResult>();
    return Run(request.behaviors ? "Saving behavior graphs" : "Saving objects", [this, request, native, result] {
        if (!HasDocument() || request.objects.empty()) { result->message = "Select objects to save"; return; }
        std::vector<CK_ID> objects;
        for (auto ref : request.objects) {
            if (!Current()->Resolve(ref, request.behaviors ? CKCID_BEHAVIOR : CKCID_OBJECT)) {
                result->message = "An export object no longer exists";
                return;
            }
            objects.push_back(ref.object);
        }
        BehaviorDescription description;
        if (request.description) {
            auto *first = CKBehavior::Cast(Current()->Resolve(request.objects.front()));
            if (!first) { result->message = "Select behaviors to save"; return; }
            description.cid = first->GetCompatibleClassID();
            description.name = !request.name.empty() ? Utf8ToAnsi(request.name) : first->GetName() ? first->GetName() : "";
            description.author = Utf8ToAnsi(request.author);
            description.description = Utf8ToAnsi(request.text);
            description.version = static_cast<float>(std::atof(request.version.c_str()));
        }
        ExportNativeObjects(*Current(), objects, request.path,
                            request.behaviors ? NativeExportKind::Behaviors : NativeExportKind::Objects,
                            request.hide, request.description ? &description : nullptr, *native, m_FileOperations);
    }, [this, native, result, completed = std::move(completed)](bool ok) {
        result->saved = native->report.saved;
        result->written = std::move(native->written);
        for (const auto &message : native->report.errors) {
            if (!result->message.empty()) result->message += "; ";
            result->message += AnsiToUtf8(message);
        }
        for (const auto &note : native->report.notes) {
            if (!result->message.empty()) result->message += "; ";
            result->message += AnsiToUtf8(note);
            m_Observer.Log(LogLevel::Warning, AnsiToUtf8(note));
        }
        result->completed = ok;
        if (completed) completed(*result);
    });
}

void DocumentFiles::WithDuplicates(const std::string &path, Load load,
                                   std::function<void(LoadResult::Stop)> cancelled) {
    auto found = std::make_shared<std::vector<DuplicateName>>();
    const auto scope = m_Runtime.CurrentDocument();
    Run(
        "Checking duplicate names",
        [this, path, found] {
            if (HasDocument()) *found = Current()->FindDuplicateNames(path);
        },
        [this, path, found, scope, load = std::move(load), cancelled = std::move(cancelled)](bool ok) mutable {
            if (!ok || !scope.IsCurrent()) {
                if (cancelled) cancelled(scope.IsCurrent() ? LoadResult::Stop::Failed : LoadResult::Stop::Stale);
                return;
            }
            if (found->empty()) {
                if (load) load(nullptr);
            } else
                m_Loads.push_back(
                    {{path, std::move(*found), ++m_NextPrompt}, scope, std::move(load), std::move(cancelled)});
        });
}

DocumentFiles::DuplicatePrompt *DocumentFiles::PendingDuplicates() {
    while (!m_Loads.empty() && !m_Loads.front().document.IsCurrent()) {
        auto load = std::move(m_Loads.front());
        m_Loads.erase(m_Loads.begin());
        if (load.cancelled) load.cancelled(LoadResult::Stop::Stale);
    }
    return m_Loads.empty() ? nullptr : &m_Loads.front().prompt;
}

void DocumentFiles::ResolveDuplicates(bool accepted) {
    if (!PendingDuplicates()) return;
    PendingLoad done = std::move(m_Loads.front());
    m_Loads.erase(m_Loads.begin());
    if (accepted && done.load)
        done.load(&done.prompt.duplicates);
    else {
        m_Observer.Log(LogLevel::Info, "Load cancelled: " + done.prompt.path);
        if (done.cancelled) done.cancelled(LoadResult::Stop::Cancelled);
    }
}

void DocumentFiles::DocumentChanged() {
    auto pending = std::move(m_Loads);
    m_Loads.clear();
    for (auto &load : pending) if (load.cancelled) load.cancelled(LoadResult::Stop::Stale);
}

} // namespace vdev
