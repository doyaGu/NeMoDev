#include <filesystem>
#include "core/graph/NmsCommands.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "core/runtime/CkRuntimeHost.h"
#include "core/schematic/InterfaceDataReconstruction.h"
#include "core/schematic/LayoutCalculator.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/document/detail/NativeExport.h"
#include "core/document/Edit.h"
#include "core/document/ScriptSession.h"
#include "core/graph/TopologyCommands.h"

namespace vdev {

bool SaveBehaviorsAsNms(Document &document, const std::vector<CK_ID> &behaviors, const std::string &path,
                        std::string *error, bool hideRepresentation) {
    return SaveNativeExport(document, behaviors, path, error, NativeExportKind::Behaviors, hideRepresentation);
}

EditResult ImportBehaviorGraph(Document &document, CK_ID script, CK_ID graph,
                               const std::string &path, const Point &position) {
    CKContext *context = document.Context();
    CKObjectArray *array = CreateCKObjectArray();
    CKERROR loaded = CKERR_INVALIDPARAMETER;
    GuardedCall([&] { loaded = context->Load(const_cast<CKSTRING>(std::filesystem::u8path(path).string().c_str()), array, CK_LOAD_DEFAULT); },
                &document.Host(), "CKContext::Load");
    std::vector<CK_ID> objects;
    for (array->Reset(); !array->EndOfList(); array->Next()) objects.push_back(array->GetDataId());
    DeleteCKObjectArray(array);
    auto discard = [&] {
        XObjectArray all;
        for (CK_ID id : objects)
            if (context->GetObject(id)) all.PushBack(id);
        GuardedCall([&] { context->DestroyObjects(all.Begin(), all.Size()); }, &document.Host(), "DestroyObjects");
    };
    if (loaded != CK_OK) {
        discard();
        document.Host().Log(LogLevel::Warning, "Cannot load " + path + " (" + std::to_string(loaded) + ")");
        return EditResult::Refused("Cannot load behavior file (" + std::to_string(loaded) + ")");
    }
    // Top-level behaviors of the file.
    // Top-level: a behavior whose parent is not in the file (a saved
    // behavior may still name its former parent when that one exists here).
    const std::set<CK_ID> fromFile(objects.begin(), objects.end());
    std::vector<CKBehavior *> tops;
    for (CK_ID id : objects) {
        CKBehavior *behavior = CKBehavior::Cast(context->GetObject(id));
        if (behavior && (!behavior->GetParent() || !fromFile.count(behavior->GetParent()->GetID())))
            tops.push_back(behavior);
    }
    for (CKBehavior *behavior : tops)
        if (behavior->GetType() & CKBEHAVIORTYPE_SCRIPT) {
            discard();
            return EditResult::Refused(
                "The file holds a Script; Import Behavior Graph takes behavior graphs and Building Blocks");
        }
    // The owner's class (Dev.exe sub_575330, sub_56B590): a behavior for
    // another class uses a target when it can, else it is left out ("No
    // Compatible Behaviors Found !", "Warning: Some Incompatible Behaviors
    // Found !").
    bool someLeft = false;
    if (CKBehavior *into = CKBehavior::Cast(context->GetObject(script)))
        if (CKBeObject *owner = into->GetOwner()) {
            std::vector<CKBehavior *> compatible;
            std::set<CK_ID> leftOut;
            for (CKBehavior *behavior : tops) {
                if (CKIsChildClassOf(owner, behavior->GetCompatibleClassID())) {
                    compatible.push_back(behavior);
                } else if (behavior->IsTargetable()) {
                    behavior->UseTarget(TRUE);
                    compatible.push_back(behavior);
                } else {
                    leftOut.insert(behavior->GetID());
                }
            }
            if (!leftOut.empty()) {
                if (compatible.empty()) {
                    discard();
                    return EditResult::Refused("No Compatible Behaviors Found !");
                }
                const std::set<CK_ID> owned = CollectOwnedObjects(context, leftOut);
                XObjectArray doomed;
                for (CK_ID id : owned)
                    if (context->GetObject(id)) doomed.PushBack(id);
                GuardedCall([&] { context->DestroyObjects(doomed.Begin(), doomed.Size()); }, &document.Host(), "DestroyObjects");
                objects.erase(std::remove_if(objects.begin(), objects.end(), [&](CK_ID id) { return owned.count(id) != 0; }),
                              objects.end());
                tops = compatible;
                someLeft = true;
            }
        }
    if (tops.empty()) {
        discard();
        std::string message = "The file holds no behavior (" + std::to_string(objects.size()) + " objects loaded";
        for (CK_ID id : objects)
            if (CKObject *object = context->GetObject(id))
                message += std::string(", ") + CKClassIDToString(object->GetClassID());
        return EditResult::Refused(message + ")");
    }
    // Display source: each top behavior as a child record of a placeholder root.
    InterfaceData source;
    source.rootBehavior = BehaviorData(0);
    float left = 0.0f;
    for (CKBehavior *behavior : tops) {
        // The loaded behavior becomes a sub-behavior of the target graph.
        behavior->SetFlags(static_cast<CK_BEHAVIOR_FLAGS>(behavior->GetFlags() & ~CKBEHAVIOR_TOPMOST));
        InterfaceData own;
        bool haveOwn = false;
        if (behavior->GetType() != CKBEHAVIORTYPE_BASE) {
            if (CKStateChunk *chunk = behavior->GetInterfaceChunk())
                haveOwn = own.LoadFromChunk(behavior, chunk) == CK_OK;
            // A chunk whose records do not cover every sub-behavior is rebuilt.
            if (haveOwn) {
                std::set<CK_ID> recorded;
                for (const auto &record : own.behaviors) recorded.insert(record.id);
                std::vector<CKBehavior *> pending{behavior};
                while (!pending.empty() && haveOwn) {
                    CKBehavior *node = pending.back();
                    pending.pop_back();
                    for (int i = 0; i < node->GetSubBehaviorCount(); ++i) {
                        CKBehavior *child = node->GetSubBehavior(i);
                        if (!child) continue;
                        if (!recorded.count(child->GetID())) haveOwn = false;
                        pending.push_back(child);
                    }
                }
                if (!haveOwn) own = InterfaceData();
            }
            if (!haveOwn) {
                ReconstructInterfaceData(own, behavior);
                haveOwn = true;
            }
        }
        BehaviorData record = haveOwn ? own.rootBehavior : BehaviorData(behavior->GetID());
        record.id = behavior->GetID();
        record.parentId = 0;
        record.depth = 1;
        record.isBehaviorGraph = behavior->GetType() != CKBEHAVIORTYPE_BASE;
        record.folded = true;
        record.UpdateRuntimeState(behavior);
        LayoutCalculator::CalculateBehaviorSize(record, behavior);
        record.rect.hPos = left;
        record.rect.vPos = 0.0f;
        left += record.rect.hSize + 40.0f;
        source.behaviors.push_back(record);
        if (haveOwn)
            for (auto child : own.behaviors) {
                // In a graph's own InterfaceChunk the root's children name parent 0.
                if (child.parentId == 0 || child.parentId == own.rootBehavior.id) child.parentId = behavior->GetID();
                child.depth += 1;
                source.behaviors.push_back(child);
            }
    }
    std::vector<CK_ID> ids;
    for (CKBehavior *behavior : tops) ids.push_back(behavior->GetID());
    // The paste and the objects it brings: one undo step.
    Edit edit(document, "Import Behavior Graph");
    const EditResult result = PasteBehaviors(document, source, ids, {}, script, graph, position);
    if (!result.ok) {
        edit.Cancel();
        discard();
        return result;
    }
    // The pasted copies stay and the loaded behaviors go; other loaded
    // objects (materials, textures, ... the copies may reference) join the
    // composition with the paste: one undo step, undone they leave the
    // saved composition (graveyard) with the copies.
    const std::set<CK_ID> originals = CollectOwnedObjects(context, std::set<CK_ID>(ids.begin(), ids.end()));
    XObjectArray behaviorsOnly;
    std::set<CK_ID> joined;
    for (CK_ID id : objects) {
        if (!context->GetObject(id)) continue;
        if (originals.count(id) || std::find(ids.begin(), ids.end(), id) != ids.end()) behaviorsOnly.PushBack(id);
        else joined.insert(id);
    }
    for (CK_ID id : ids)
        if (std::find(behaviorsOnly.Begin(), behaviorsOnly.End(), id) == behaviorsOnly.End()) behaviorsOnly.PushBack(id);
    GuardedCall([&] { context->DestroyObjects(behaviorsOnly.Begin(), behaviorsOnly.Size()); }, &document.Host(),
                "DestroyObjects");
    for (CK_ID id : joined) edit.Created(id);
    for (CK_ID id : CollectOwnedObjects(context, joined)) edit.Created(id);
    const EditResult committed = edit.Commit(result.message, result.created);
    if (!committed.ok) return committed;
    EditResult done = result;
    if (someLeft) done.message += " - Warning: Some Incompatible Behaviors Found !";
    return done;
}

} // namespace vdev
