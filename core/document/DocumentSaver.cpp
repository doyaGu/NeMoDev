#include "core/document/DocumentSaver.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif

#include "core/document/Document.h"
#include "core/document/ObjectPictures.h"
#include "core/document/SpriteTextPersistence.h"
#include "core/document/detail/ProjectParameterTypes.h"
#include "core/interface/InterfaceDataDiff.h"
#include "core/interface/InterfaceDataValidation.h"
#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/SdkCompat.h"

namespace vdev {

// Owns only the candidate file. The destination is never removed before a
// successful same-directory publication; CK writing and verification stay below.
class PendingSaveFile {
public:
    PendingSaveFile(const std::filesystem::path &destination, SaveReport &report)
        : m_Destination(destination), m_Report(report) {}
    PendingSaveFile(const PendingSaveFile &) = delete;
    PendingSaveFile &operator=(const PendingSaveFile &) = delete;
    ~PendingSaveFile() {
        if (m_Path.empty())
            return;
        std::error_code error;
        std::filesystem::remove(m_Path, error);
        if (error)
            m_Report.notes.push_back("Cannot remove temporary composition " + m_Path.string() + ": " + error.message());
    }
    bool Create() {
        std::random_device random;
        for (int attempt = 0; attempt < 64; ++attempt) {
            std::filesystem::path name(".nemodev-save-" + std::to_string(random()) + "-" + std::to_string(random()));
            name += m_Destination.extension();
            auto candidate = m_Destination.parent_path() / name;
#ifdef _WIN32
            HANDLE file =
                CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                const DWORD error = GetLastError();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
                    continue;
                return Fail("Cannot create a temporary composition in the destination directory",
                            std::error_code(error, std::system_category()));
            }
            CloseHandle(file);
#else
            const int file = open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
            if (file < 0) {
                const int error = errno;
                if (error == EEXIST)
                    continue;
                return Fail("Cannot create a temporary composition in the destination directory",
                            std::error_code(error, std::generic_category()));
            }
            close(file);
#endif
            m_Path = std::move(candidate);
            return PreservePermissions();
        }
        m_Report.errors.push_back("Cannot choose an unused temporary composition name");
        return false;
    }
    const std::filesystem::path &Path() const { return m_Path; }
    bool Publish(bool allowOverwrite) {
        if (allowOverwrite && !PreservePermissions())
            return false;
#ifdef _WIN32
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (allowOverwrite ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!MoveFileExW(m_Path.c_str(), m_Destination.c_str(), flags))
            return Fail("Cannot replace the destination composition",
                        std::error_code(GetLastError(), std::system_category()));
        m_Path.clear();
#else
        if (allowOverwrite) {
            std::error_code error;
            const auto status = std::filesystem::status(m_Destination, error);
            if (error && error != std::errc::no_such_file_or_directory)
                return Fail("Cannot inspect the destination composition", error);
            if (std::filesystem::exists(status)) {
                if (access(m_Destination.c_str(), W_OK) != 0)
                    return Fail("Cannot write the destination composition",
                                std::error_code(errno, std::generic_category()));
            }
            if (rename(m_Path.c_str(), m_Destination.c_str()) != 0)
                return Fail("Cannot replace the destination composition",
                            std::error_code(errno, std::generic_category()));
            m_Path.clear();
        } else {
            // Refuse an existing destination, including one created during CK
            // callbacks. These operations do not need filesystem hard-link support.
#if defined(__linux__)
            if (syscall(SYS_renameat2, AT_FDCWD, m_Path.c_str(), AT_FDCWD, m_Destination.c_str(), RENAME_NOREPLACE) !=
                0)
                return Fail("Cannot create the destination composition",
                            std::error_code(errno, std::generic_category()));
            m_Path.clear();
#elif defined(__APPLE__)
            if (renamex_np(m_Path.c_str(), m_Destination.c_str(), RENAME_EXCL) != 0)
                return Fail("Cannot create the destination composition",
                            std::error_code(errno, std::generic_category()));
            m_Path.clear();
#else
            if (link(m_Path.c_str(), m_Destination.c_str()) != 0)
                return Fail("Cannot create the destination composition",
                            std::error_code(errno, std::generic_category()));
                // The remaining candidate name is removed by this owner's destructor.
#endif
        }
#endif
        return true;
    }

private:
    std::filesystem::path m_Destination, m_Path;
    SaveReport &m_Report;
    bool PreservePermissions() {
#ifdef _WIN32
        DWORD size = 0;
        GetFileSecurityW(m_Destination.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return true;
        if (error != ERROR_INSUFFICIENT_BUFFER)
            return Fail("Cannot read the destination composition permissions",
                        std::error_code(error, std::system_category()));
        std::vector<unsigned char> security(size);
        auto *descriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(security.data());
        if (!GetFileSecurityW(m_Destination.c_str(), DACL_SECURITY_INFORMATION, descriptor, size, &size))
            return Fail("Cannot read the destination composition permissions",
                        std::error_code(GetLastError(), std::system_category()));
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        if (!GetSecurityDescriptorControl(descriptor, &control, &revision))
            return Fail("Cannot read the destination composition permissions",
                        std::error_code(GetLastError(), std::system_category()));
        const SECURITY_INFORMATION inheritance =
            control & SE_DACL_PROTECTED ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;
        if (!SetFileSecurityW(m_Path.c_str(), DACL_SECURITY_INFORMATION | inheritance, descriptor))
            return Fail("Cannot preserve the destination composition permissions",
                        std::error_code(GetLastError(), std::system_category()));
#else
        std::error_code error;
        const auto status = std::filesystem::status(m_Destination, error);
        if (error && error != std::errc::no_such_file_or_directory)
            return Fail("Cannot read the destination composition permissions", error);
        if (std::filesystem::exists(status)) {
            std::filesystem::permissions(m_Path, status.permissions(), error);
            if (error)
                return Fail("Cannot preserve the destination composition permissions", error);
        }
#endif
        return true;
    }
    bool Fail(const char *message, const std::error_code &error) {
        m_Report.errors.push_back(std::string(message) + ": " + error.message());
        return false;
    }
};

namespace {

namespace fs = std::filesystem;

struct ChunkDeleter {
    void operator()(CKStateChunk *chunk) const { DeleteCKStateChunk(chunk); }
};
using Chunk = std::unique_ptr<CKStateChunk, ChunkDeleter>;

struct Replacement {
    CK_ID id = 0;
    CKBehavior *script = nullptr;
    Chunk chunk;
    size_t behaviorCount = 0;
    size_t linkCount = 0;
    Chunk restoreChunk;
};

// Snapshot omission is an output option. Keep the complete chunk available
// for the next save, including when CK writing or verification fails. This
// owner lives outside GuardedCall so SEH cannot skip the restoration.
class RestoreSnapshotChunks {
public:
    RestoreSnapshotChunks(Document &document, std::vector<Replacement> &replacements)
        : m_Document(document), m_Context(document.Context()), m_Generation(document.Host().Generation()),
          m_Replacements(replacements) {}
    RestoreSnapshotChunks(const RestoreSnapshotChunks &) = delete;
    RestoreSnapshotChunks &operator=(const RestoreSnapshotChunks &) = delete;
    ~RestoreSnapshotChunks() { Restore(); }
    void Restore() {
        if (m_Document.Context() != m_Context || m_Document.Host().Generation() != m_Generation) return;
        for (auto &replacement : m_Replacements)
            if (replacement.restoreChunk && m_Context->GetObject(replacement.id) == replacement.script)
                replacement.script->SetInterfaceChunk(replacement.restoreChunk.release());
    }

private:
    Document &m_Document;
    CKContext *m_Context;
    std::uint64_t m_Generation;
    std::vector<Replacement> &m_Replacements;
};

size_t CountLinks(const InterfaceData &data) {
    size_t count = data.rootBehavior.links.size();
    for (const auto &record : data.behaviors) count += record.links.size();
    return count;
}

std::string NameOf(CKObject *object) {
    return object && object->GetName() ? object->GetName() : std::string();
}

Chunk SerializeScript(InterfaceData &data, CKBehavior *script, bool reconstructed, SaveReport &report) {
    const std::string name = NameOf(script);
    const auto issues = ValidateInterfaceData(data, script, InterfaceDataValidationStage::BeforeSaveToChunk);
    if (!issues.empty()) {
        for (const auto &issue : issues)
            report.errors.push_back(name + ": behavior " + std::to_string(issue.behaviorId) + " link " +
                                    std::to_string(issue.linkIndex) + " " + issue.field + ": " + issue.reason);
        return {};
    }
    Chunk chunk(GenerateInterfaceChunk(data, script));
    if (!chunk) {
        report.errors.push_back(name + ": InterfaceChunk generation failed");
        return {};
    }
    InterfaceData reloaded;
    const CKERROR error = reloaded.LoadFromChunk(script, chunk.get());
    InterfaceDataDiff::Config config;
    // Parameter::id of rebuilt shortcuts is not stored in display tables.
    config.ignoreSyntheticParameterIds = reconstructed;
    const auto diff = InterfaceDataDiff(config).Compare(data, reloaded);
    if (error != CK_OK || !diff.identical) {
        if (std::getenv("SCHEMATIC_SAVE_TRACE")) std::fprintf(stderr, "%s\n", diff.GetDetailedReport().c_str());
        report.errors.push_back(name + ": generated InterfaceChunk does not reload identically");
        return {};
    }
    return chunk;
}

} // namespace

// The sets behind Document's queries, which only the saver reads.
struct DocumentSaverAccess {
    static const std::set<CK_ID> &Graveyard(const Document &document) { return document.m_Graveyard; }
};

// Only tracked composition edits participate in its save point. Navigation
// and opening an unchanged Script session do not change the written content.
struct DocumentSaveRevisions {
    std::uint64_t generation, topology;
    std::map<CK_ID, std::uint64_t> scripts;
    explicit DocumentSaveRevisions(const Document &document)
        : generation(document.Host().Generation()), topology(document.TopologyRevision()) {
        for (const auto &entry : document.Sessions()) scripts.emplace(entry.first, entry.second->Revision());
    }
    bool Unchanged(const Document &document) const {
        if (!document.IsOpen() || document.EditOpen() || document.Host().Generation() != generation ||
            document.TopologyRevision() != topology) return false;
        for (const auto &entry : scripts) {
            const auto found = document.Sessions().find(entry.first);
            if (found == document.Sessions().end() || found->second->Revision() != entry.second) return false;
        }
        for (const auto &entry : document.Sessions())
            if (!scripts.count(entry.first) && entry.second->Dirty()) return false;
        return true;
    }
};

namespace {

// Graveyard objects plus everything they own (IOs, parameters, links,
// operations and sub-behaviors), which the load array lists separately.
std::set<CK_ID> ExcludedObjects(CKContext *context, const std::set<CK_ID> &graveyard) {
    std::set<CK_ID> excluded;
    std::vector<CKObject *> pending;
    for (CK_ID id : graveyard)
        if (CKObject *object = context->GetObject(id)) pending.push_back(object);
    while (!pending.empty()) {
        CKObject *object = pending.back();
        pending.pop_back();
        if (!object || !excluded.insert(object->GetID()).second) continue;
        if (CKBehavior *behavior = CKBehavior::Cast(object)) {
            for (int i = 0; i < behavior->GetInputCount(); ++i) pending.push_back(behavior->GetInput(i));
            for (int i = 0; i < behavior->GetOutputCount(); ++i) pending.push_back(behavior->GetOutput(i));
            for (int i = 0; i < behavior->GetInputParameterCount(); ++i)
                pending.push_back(behavior->GetInputParameter(i));
            for (int i = 0; i < behavior->GetOutputParameterCount(); ++i)
                pending.push_back(behavior->GetOutputParameter(i));
            for (int i = 0; i < behavior->GetLocalParameterCount(); ++i)
                pending.push_back(behavior->GetLocalParameter(i));
            if (behavior->GetTargetParameter()) pending.push_back(behavior->GetTargetParameter());
            for (int i = 0; i < behavior->GetSubBehaviorCount(); ++i) pending.push_back(behavior->GetSubBehavior(i));
            for (int i = 0; i < behavior->GetSubBehaviorLinkCount(); ++i)
                pending.push_back(behavior->GetSubBehaviorLink(i));
            for (int i = 0; i < behavior->GetParameterOperationCount(); ++i)
                pending.push_back(behavior->GetParameterOperation(i));
        } else if (CKParameterOperation *operation = CKParameterOperation::Cast(object)) {
            pending.push_back(operation->GetInParameter1());
            pending.push_back(operation->GetInParameter2());
            pending.push_back(operation->GetOutParameter());
        }
    }
    return excluded;
}

// Object parameters may refer to objects that Building Blocks or managers
// create while loading: never in the file, recreated with new IDs by every
// load (Find Path.cmo FindPathGroup, flagged CK_OBJECT_NOTTOBESAVED; Vertex
// Render.cmo MLMaterial and IK Position.cmo IKPosChain, not flagged). Such a
// reference is recorded as this marker on both sides. A reference to an
// object loaded from the source file or created by the editor must survive
// the save exactly: the reloaded value then maps to a saved ID.
constexpr CK_ID RUNTIME_ONLY_OBJECT = 0xFFFFFFFF;

void MarkRuntimeOnlyReferences(Document &document, ProjectParameterTypes::Snapshot &snapshot) {
    std::set<CK_ID> documentObjects(document.LoadedObjects().begin(), document.LoadedObjects().end());
    documentObjects.insert(document.CreatedObjects().begin(), document.CreatedObjects().end());
    CKContext *context = document.Context();
    for (auto &entry : snapshot) {
        ProjectParameterTypes::State &state = entry.second;
        if (!state.hasObjectValue || !state.objectValue) continue;
        CKObject *object = context->GetObject(state.objectValue);
        const bool runtimeOnly = !documentObjects.count(state.objectValue) ||
                                 (object && (object->GetObjectFlags() & CK_OBJECT_NOTTOBESAVED));
        if (runtimeOnly) state.objectValue = RUNTIME_ONLY_OBJECT;
    }
}

// Snapshot of the reloaded file keyed by saved IDs (CKFileObject::Object).
bool CaptureSaved(CKFile &file, ProjectParameterTypes::Snapshot &result, std::ostream &errors) {
    ProjectParameterTypes::Snapshot runtime;
    if (!ProjectParameterTypes::Capture(file, false, runtime, errors)) return false;
    std::map<CK_ID, CK_ID> saved;
    for (int i = 0; i < file.m_FileObjects.Size(); ++i)
        if (file.m_FileObjects[i].ObjPtr) saved[file.m_FileObjects[i].ObjPtr->GetID()] = file.m_FileObjects[i].Object;
    result.clear();
    for (auto entry : runtime) {
        auto key = saved.find(entry.first);
        if (key == saved.end()) {
            errors << "Parameter has no saved ID: " << entry.second.name << '\n';
            return false;
        }
        ProjectParameterTypes::State &state = entry.second;
        // A <This> parameter takes its Script's owner, and the owner's class as
        // its type, when the file loads (none for a Script no object holds:
        // Attach Script.cmo's): value and type not compared.
        if (CKParameterLocal *local = CKParameterLocal::Cast(file.m_Context->GetObject(entry.first)))
            if (local->IsMyselfParameter()) {
                state.hasObjectValue = false;
                state.objectValue = 0;
                state.guid = CKGUID();
            }
        if (state.hasObjectValue && state.objectValue) {
            auto value = saved.find(state.objectValue);
            // Not a saved object: created by the reload itself. The comparison
            // fails unless the original also referred to a runtime object.
            state.objectValue = value != saved.end() ? value->second : RUNTIME_ONLY_OBJECT;
        }
        result.emplace(key->second, state);
    }
    return true;
}

// A file for the player (CKFILE_FORVIEWER) has no InterfaceChunk to check and
// is loaded as a player loads it, outside interface mode.
bool Verify(Document &document, const std::string &path, const ProjectParameterTypes::Snapshot &expected,
            const std::vector<Replacement> &replacements, const std::map<CK_ID, int> &alignments,
            bool forViewer, SaveReport &report) {
    CKContext *verification = nullptr;
    const CKERROR created = CreateCKContext(&verification, nullptr);
    if (created != CK_OK || !verification) {
        report.errors.push_back("Cannot create the verification CKContext");
        return false;
    }
    if (!forViewer) verification->SetInterfaceMode(TRUE, nullptr, nullptr);
    CKFile *file = verification->CreateCKFile();
    CKObjectArray *objects = CreateCKObjectArray();
    CKERROR loaded = CKERR_INVALIDFILE;
    const bool completed = GuardedCall([&] {
                    loaded = file->Load(const_cast<CKSTRING>(path.c_str()), objects, CK_LOAD_DEFAULT);
                    if (loaded == CK_OK && !forViewer) RestoreSpriteTextAlignments(*file);
                },
                &document.Host(), "verification CKFile::Load");
    bool ok = completed && loaded == CK_OK;
    if (!completed) report.errors.push_back("The saved file raised an exception while loading in a new CKContext");
    else if (!ok) report.errors.push_back("The saved file does not load in a new CKContext: " + std::to_string(loaded));
    // Objects the load destroyed again (Scripts of a level scene) leave stale
    // pointers in the file's object list.
    for (int i = 0; ok && i < file->m_FileObjects.Size(); ++i) {
        const CKFileObject &entry = file->m_FileObjects[i];
        if (entry.ObjPtr && verification->GetObject(entry.CreatedObject) != entry.ObjPtr) {
            CKObject *original = document.Context()->GetObject(entry.Object);
            report.errors.push_back("Objects are lost when the saved file loads, first: " + NameOf(original) +
                                    " (class " + std::to_string(entry.ObjectCid) + ")");
            ok = false;
        }
    }

    if (ok) {
        std::ostringstream errors;
        ProjectParameterTypes::Snapshot actual;
        if (!CaptureSaved(*file, actual, errors) ||
            !ProjectParameterTypes::Compare(expected, actual, errors)) {
            report.errors.push_back("Parameter types, object references or enum values changed: " + errors.str());
            ok = false;
            if (std::getenv("SCHEMATIC_SAVE_TRACE"))
                for (const auto &item : expected) {
                    const auto found = actual.find(item.first);
                    if (found == actual.end() || (item.second.objectValue == found->second.objectValue &&
                                                  item.second.guid == found->second.guid))
                        continue;
                    CKParameterLocal *local = CKParameterLocal::Cast(document.Context()->GetObject(item.first));
                    std::fprintf(stderr, "differs: %s id=%u object %u -> %u, myself %d, owner %u\n", item.second.name.c_str(),
                                 (unsigned) item.first, (unsigned) item.second.objectValue, (unsigned) found->second.objectValue,
                                 local && local->IsMyselfParameter() ? 1 : 0,
                                 local && local->GetOwner() ? (unsigned) local->GetOwner()->GetID() : 0u);
                    break;
                }
            if (std::getenv("SCHEMATIC_SAVE_TRACE"))
                for (int i = 0; i < file->m_FileObjects.Size(); ++i) {
                    const auto &entry = file->m_FileObjects[i];
                    if (entry.ObjPtr) continue;
                    CKObject *original = document.Context()->GetObject(entry.Object);
                    CKObject *owner = nullptr;
                    if (CKParameter *parameter = CKParameter::Cast(original)) owner = parameter->GetOwner();
                    if (CKParameterIn *input = CKParameterIn::Cast(original)) owner = input->GetOwner();
                    std::fprintf(stderr, "not loaded %u class %d '%s' owner %u '%s' graveyard %d/%d\n",
                                 static_cast<unsigned>(entry.Object), entry.ObjectCid,
                                 original && original->GetName() ? original->GetName() : "",
                                 owner ? static_cast<unsigned>(owner->GetID()) : 0u,
                                 owner && owner->GetName() ? owner->GetName() : "",
                                 static_cast<int>(document.IsDiscarded(entry.Object)),
                                 owner ? static_cast<int>(document.IsDiscarded(owner->GetID())) : -1);
                }
        }
        report.parametersVerified = static_cast<int>(actual.size());
    }
    if (ok && !forViewer) {
        if (CaptureSpriteTextAlignments(*file) != alignments) {
            report.errors.push_back("Sprite Text alignments changed when the saved file loaded in a new CKContext");
            ok = false;
        } else {
            report.spriteTextsVerified = static_cast<int>(alignments.size());
        }
    }
    if (ok && !forViewer) {
        // CKFileObject::Object is the saved ID, i.e. the runtime ID at save time.
        std::map<CK_ID, CKObject *> bySavedId;
        for (int i = 0; i < file->m_FileObjects.Size(); ++i)
            if (file->m_FileObjects[i].ObjPtr) bySavedId[file->m_FileObjects[i].Object] = file->m_FileObjects[i].ObjPtr;
        for (const auto &replacement : replacements) {
            auto found = bySavedId.find(replacement.id);
            CKBehavior *script = found != bySavedId.end() ? CKBehavior::Cast(found->second) : nullptr;
            InterfaceData reloaded;
            CKERROR extracted = CKERR_NOTFOUND;
            if (script)
                GuardedCall([&] { extracted = ExtractInterfaceData(script, reloaded); }, &document.Host(),
                            "verification ExtractInterfaceData");
            if (extracted != CK_OK || reloaded.behaviors.size() != replacement.behaviorCount ||
                CountLinks(reloaded) != replacement.linkCount) {
                report.errors.push_back("Rewritten InterfaceChunk does not reload: " + NameOf(replacement.script));
                ok = false;
                continue;
            }
            ++report.scriptsVerified;
        }
    }
    verification->DeleteCKFile(file);
    DeleteCKObjectArray(objects);
    if (!forViewer) verification->SetInterfaceMode(FALSE, nullptr, nullptr);
    CKCloseContext(verification);
    return ok;
}

} // namespace

bool SaveDocumentAs(Document &document, const std::string &path, const SaveOptions &options,
                    SaveReport &report) {
    report = SaveReport();
    CKContext *context = document.Context();
    if (!document.IsOpen() || !context) {
        report.errors.push_back("No composition is open");
        return false;
    }
    if (document.EditOpen()) {
        report.errors.push_back("Finish the current edit before saving the composition");
        return false;
    }
    std::error_code ec;
    fs::path output = fs::absolute(fs::u8path(path), ec);
    if (ec) {
        report.errors.push_back("Cannot resolve the output path: " + ec.message());
        return false;
    }
    std::string outputName;
    try { outputName = output.string(); }
    catch (const std::system_error &) {
        report.errors.push_back("The output path cannot be represented in the native character set");
        return false;
    }
    const fs::path source = fs::absolute(fs::u8path(document.SourcePath()), ec);
    if (output == source || fs::equivalent(output, source, ec)) {
        report.errors.push_back("The loaded source file is never overwritten; choose a new file");
        return false;
    }
    if (fs::exists(output, ec) && !options.allowOverwrite) {
        report.errors.push_back("The output file already exists");
        return false;
    }
    // Follow an existing target link as the previous direct write did. A
    // candidate belongs beside the actual destination, on the same filesystem.
    output = fs::weakly_canonical(output, ec);
    if (ec) {
        report.errors.push_back("Cannot resolve the output directory: " + ec.message());
        return false;
    }
    // weakly_canonical cannot follow a dangling final symlink. Preserve the old
    // file API's behavior of creating its referent, rather than replacing the link.
    for (int links = 0; fs::is_symlink(fs::symlink_status(output, ec)); ++links) {
        if (links == 40) {
            report.errors.push_back("Too many symbolic links in the output path");
            return false;
        }
        const auto link = fs::read_symlink(output, ec);
        if (!ec) output = fs::weakly_canonical(link.is_absolute() ? link : output.parent_path() / link, ec);
        if (ec) {
            report.errors.push_back("Cannot resolve the output link: " + ec.message());
            return false;
        }
    }
    PendingSaveFile pending(output, report);
    if (!pending.Create()) return false;
    try { outputName = pending.Path().string(); }
    catch (const std::system_error &) {
        report.errors.push_back("The temporary output path cannot be represented in the native character set");
        return false;
    }

    // 1. Serialize and reload every edited Script before installing anything.
    std::vector<Replacement> replacements;
    // Without Save Script Snapshot every Script with a snapshot is written
    // again without it (its session made now if needed).
    if (!options.saveScriptSnapshots)
        for (const ScriptInfo &info : document.Scripts())
            if (info.hasInterfaceChunk && !document.IsDiscarded(info.id)) document.Session(info.id);
    const DocumentSaveRevisions revisions(document);
    for (const auto &entry : document.Sessions()) {
        ScriptSession &session = *entry.second;
        if (session.ReadOnly()) continue;
        const bool dropSnapshot = !options.saveScriptSnapshots && session.Data().header.snapshot != nullptr;
        if (!session.Dirty() && !dropSnapshot) continue;
        CKBehavior *script = CKBehavior::Cast(context->GetObject(entry.first));
        if (!script || document.IsDiscarded(entry.first)) continue;
        InterfaceData written = session.Data();
        if (dropSnapshot) written.header.SetSnapshot(nullptr);
        const bool reconstructed = session.Origin() == SessionOrigin::Reconstructed;
        Chunk chunk = SerializeScript(written, script, reconstructed, report);
        if (!chunk) return false;
        Chunk restoreChunk;
        if (dropSnapshot) {
            // Preserve untouched bytes. Edited layouts need a complete new
            // chunk too, since a successful save will mark the session clean.
            if (!session.Dirty() && script->GetInterfaceChunk()) {
                restoreChunk.reset(CreateCKStateChunk(script->GetInterfaceChunk()));
                if (!restoreChunk) report.errors.push_back(NameOf(script) + ": cannot retain the complete InterfaceChunk");
            } else {
                InterfaceData complete = session.Data();
                restoreChunk = SerializeScript(complete, script, reconstructed, report);
            }
            if (!restoreChunk) return false;
        }
        replacements.push_back({entry.first, script, std::move(chunk), written.behaviors.size(), CountLinks(written),
                                std::move(restoreChunk)});
    }

    // 2. Install. CKBehavior owns installed chunks.
    RestoreSnapshotChunks restoreSnapshots(document, replacements);
    for (auto &replacement : replacements) replacement.script->SetInterfaceChunk(replacement.chunk.release());
    report.chunksWritten = static_cast<int>(replacements.size());

    // 3. Save the loaded and created objects, minus soft-deleted ones.
    WriteSpriteTextAlignments(document);
    const std::set<CK_ID> excluded = ExcludedObjects(context, DocumentSaverAccess::Graveyard(document));
    CKObjectArray *objects = CreateCKObjectArray();
    std::set<CK_ID> listed;
    auto list = [&](CK_ID id) {
        if (!excluded.count(id) && context->GetObject(id) && listed.insert(id).second) objects->InsertRear(id);
    };
    // The current level first: loading merges the file's level into the
    // level of the context, so it is not among the loaded objects; without
    // it the scenes (settings, objects, initial conditions) are not saved.
    if (CKLevel *level = context->GetCurrentLevel()) list(level->GetID());
    for (CK_ID id : document.LoadedObjects()) list(id);
    for (CK_ID id : document.CreatedObjects()) list(id);
    const CK_FILE_WRITEMODE writeMode = context->GetFileWriteMode();
    const bool forViewer = ((writeMode | options.writeMode) & CKFILE_FORVIEWER) != 0;
    context->SetFileWriteMode(static_cast<CK_FILE_WRITEMODE>(writeMode | options.writeMode));
    CKFile *file = context->CreateCKFile();
    CKERROR saved = CKERR_INVALIDFILE;
    ProjectParameterTypes::Snapshot expected;
    std::map<CK_ID, int> alignments;
    std::ostringstream captureErrors;
    bool captured = false;
    // The Building Blocks' and managers' save callbacks run here: one that
    // raises fails the save instead of ending the editor with the work unsaved.
    const bool completed = GuardedCall(
        [&] {
            saved = file->StartSave(const_cast<CKSTRING>(outputName.c_str()));
            if (saved != CK_OK) return;
            objects->Reset();
            file->SaveObjects(objects);
            alignments = CaptureSpriteTextAlignments(*file);
            // The saving CKFile lists every written object (with dependencies)
            // until EndSave; saved IDs are the current runtime IDs.
            captured = ProjectParameterTypes::Capture(*file, false, expected, captureErrors);
            // <This> values and types come from the owners at load (CaptureSaved): not compared.
            for (auto &item : expected)
                if (CKParameterLocal *local = CKParameterLocal::Cast(context->GetObject(item.first)))
                    if (local->IsMyselfParameter()) {
                        item.second.hasObjectValue = false;
                        item.second.objectValue = 0;
                        item.second.guid = CKGUID();
                    }
            MarkRuntimeOnlyReferences(document, expected);
            saved = file->EndSave();
        },
        &document.Host(), "CKFile::Save");
    const bool closed = GuardedCall([&] { context->DeleteCKFile(file); }, &document.Host(), "CKContext::DeleteCKFile");
    DeleteCKObjectArray(objects);
    context->SetFileWriteMode(writeMode);
    restoreSnapshots.Restore();
    if (!closed) {
        report.errors.push_back("CKFile cleanup raised an exception; the destination was not replaced");
        return false;
    }
    if (!completed) {
        report.errors.push_back("CKFile save raised an exception (the file may be incomplete)");
        return false;
    }
    if (saved != CK_OK) {
        report.errors.push_back("CKFile save failed: " + std::to_string(saved));
        return false;
    }
    // A composition without Scripts (File > New) may have no parameters to compare.
    if (!captured || (expected.empty() && !document.Scripts().empty())) {
        report.errors.push_back("Cannot capture saved parameter types: " + captureErrors.str());
        return false;
    }
    // 4. Independent reload.
    if (!Verify(document, outputName, expected, replacements, alignments, forViewer, report)) {
        report.notes.push_back("The temporary composition failed verification; the destination was not replaced");
        return false;
    }
    // 5. Publish only the verified file. Failed writes, verification and
    // replacement leave the previous destination and save point intact.
    if (!pending.Publish(options.allowOverwrite)) return false;
    report.saved = true;
    if (options.markSaved) {
        if (revisions.Unchanged(document)) {
            document.MarkSaved();
            report.documentSaved = true;
        } else {
            // A callback can undo back to the previous save point. That older
            // content must not appear saved after its file has been replaced.
            if (document.IsOpen() && document.Host().Generation() == revisions.generation && !document.Dirty())
                document.MarkTopologyChanged();
            report.notes.push_back("The file was saved, but the document changed during saving; newer edits remain unsaved");
        }
        if (document.IsOpen() && document.Host().Generation() == revisions.generation)
            document.History().MarkSavePoint();
    }
    report.notes.push_back("Native Dev load/save/reopen has not been performed");
    return true;
}

bool SaveComposition(Document &document, const std::string &path, const CompositionData &data,
                     const SaveOptions &options, SaveReport &report) {
    if (data.layout) WriteLayoutInterfaceData(document, *data.layout);
    if (data.pictures) WriteObjectPictures(document, *data.pictures);
    return SaveDocumentAs(document, path, options, report);
}

} // namespace vdev
