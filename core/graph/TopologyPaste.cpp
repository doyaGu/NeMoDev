// Paste (PasteBehaviors): the copies of behaviors and Comments, object by
// object (MapBehaviorCopy, used by ScriptCommands.cpp too).
#include "core/graph/TopologyCommands.h"

#include <algorithm>
#include <cfloat>
#include <map>

#include "core/runtime/CkRuntimeHost.h"
#include "core/interface/InterfaceChunkFormat.h"
#include "core/schematic/LayoutCalculator.h"
#include "core/schematic/LinkEndpointGeometry.h"
#include "core/schematic/ParameterDisplayGeometry.h"
#include "core/catalog/ParameterCatalog.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/schematic/SchematicEdits.h"
#include "core/schematic/SchematicGrid.h"
#include "core/graph/detail/TopologyInternal.h"

namespace vdev {

using namespace detail;

namespace {

// CK_ID of every object of a copy, by parallel traversal of the original
// (CKContext::CopyObjects returns only the top-level copies).
bool MapCopy(CKBehavior *original, CKBehavior *copy, std::map<CK_ID, CK_ID> &map) {
    if (!original || !copy) return false;
    map[original->GetID()] = copy->GetID();
    auto pair = [&](CKObject *a, CKObject *b) {
        if (!a || !b) return a == b;
        map[a->GetID()] = b->GetID();
        return true;
    };
    if (original->GetInputCount() != copy->GetInputCount() || original->GetOutputCount() != copy->GetOutputCount() ||
        original->GetInputParameterCount() != copy->GetInputParameterCount() ||
        original->GetOutputParameterCount() != copy->GetOutputParameterCount() ||
        original->GetLocalParameterCount() != copy->GetLocalParameterCount() ||
        original->GetSubBehaviorLinkCount() != copy->GetSubBehaviorLinkCount() ||
        original->GetParameterOperationCount() != copy->GetParameterOperationCount() ||
        original->GetSubBehaviorCount() != copy->GetSubBehaviorCount())
        return false;
    bool ok = true;
    for (int i = 0; i < original->GetInputCount(); ++i) ok &= pair(original->GetInput(i), copy->GetInput(i));
    for (int i = 0; i < original->GetOutputCount(); ++i) ok &= pair(original->GetOutput(i), copy->GetOutput(i));
    for (int i = 0; i < original->GetInputParameterCount(); ++i)
        ok &= pair(original->GetInputParameter(i), copy->GetInputParameter(i));
    for (int i = 0; i < original->GetOutputParameterCount(); ++i)
        ok &= pair(original->GetOutputParameter(i), copy->GetOutputParameter(i));
    for (int i = 0; i < original->GetLocalParameterCount(); ++i)
        ok &= pair(original->GetLocalParameter(i), copy->GetLocalParameter(i));
    ok &= pair(original->GetTargetParameter(), copy->GetTargetParameter());
    for (int i = 0; i < original->GetSubBehaviorLinkCount(); ++i)
        ok &= pair(original->GetSubBehaviorLink(i), copy->GetSubBehaviorLink(i));
    for (int i = 0; i < original->GetParameterOperationCount(); ++i) {
        CKParameterOperation *a = original->GetParameterOperation(i), *b = copy->GetParameterOperation(i);
        ok &= pair(a, b);
        if (a && b) {
            ok &= pair(a->GetInParameter1(), b->GetInParameter1());
            ok &= pair(a->GetInParameter2(), b->GetInParameter2());
            ok &= pair(a->GetOutParameter(), b->GetOutParameter());
        }
    }
    for (int i = 0; i < original->GetSubBehaviorCount() && ok; ++i)
        ok &= MapCopy(original->GetSubBehavior(i), copy->GetSubBehavior(i), map);
    return ok;
}

CK_ID Mapped(const std::map<CK_ID, CK_ID> &map, CK_ID id) {
    auto found = map.find(id);
    return found != map.end() ? found->second : id;
}

void RemapEndpoint(const std::map<CK_ID, CK_ID> &map, LinkEndpoint &endpoint) {
    endpoint.id = Mapped(map, endpoint.id);
}

} // namespace

namespace detail {
bool MapBehaviorCopy(CKBehavior *original, CKBehavior *copy, std::map<CK_ID, CK_ID> &map) {
    return MapCopy(original, copy, map);
}
} // namespace detail

EditResult PasteBehaviors(Document &document, const InterfaceData &source,
                          const std::vector<CK_ID> &selected, const std::vector<Comment> &comments, CK_ID scriptId,
                          CK_ID graphId, const Point &position) {
    TopologyEdit edit(document, scriptId, "Paste");
    if (!edit.Valid()) return edit.Fail(edit.Error());
    CKContext *context = edit.Context();
    CKBehavior *graph = BehaviorOf(context, graphId);
    if (!EditableGraph(graph) || !FindRecord(edit.Data(), graphId) || !TreeIds(edit.Script()).count(graphId))
        return edit.Fail("Paste into the Script or an expanded behavior graph");
    if (!context->GetCurrentLevel()) return edit.Fail("CKContext::CopyObjects needs a current level");

    // Top-level selected behaviors, in record order.
    std::set<CK_ID> chosen(selected.begin(), selected.end());
    std::vector<const BehaviorData *> tops;
    for (const auto &record : source.behaviors) {
        if (!chosen.count(record.id) || !BehaviorOf(context, record.id)) continue;
        bool nested = false;
        for (CK_ID parent = record.parentId; parent && !nested;) {
            nested = chosen.count(parent) != 0;
            const BehaviorData *up = FindRecord(source, parent);
            parent = up ? up->parentId : 0;
        }
        if (!nested) tops.push_back(&record);
    }
    if (tops.empty() && comments.empty()) return edit.Fail("Nothing to paste");

    // Offset: the selection's top-left corner goes to the snapped position.
    float left = FLT_MAX, top = FLT_MAX;
    for (const BehaviorData *record : tops) {
        left = std::min(left, record->rect.hPos);
        top = std::min(top, record->rect.vPos);
    }
    for (const auto &comment : comments) {
        left = std::min(left, comment.hPos);
        top = std::min(top, comment.vPos);
    }
    const float dh = SchematicGrid::SnapToGrid(position.h, GRID) - left;
    const float dv = SchematicGrid::SnapToGrid(position.v, GRID) - top;

    std::map<CK_ID, CK_ID> map;
    std::vector<CK_ID> copiedTops;
    if (!tops.empty()) {
        XObjectArray originals;
        for (const BehaviorData *record : tops) originals.PushBack(record->id);
        XObjectArray copies;
        const bool copied = GuardedCall(
            [&] { copies = context->CopyObjects(originals, CKGetDefaultClassDependencies(CK_DEPENDENCIES_COPY)); },
            &document.Host(), "CKContext::CopyObjects");
        for (int i = 0; i < copies.Size(); ++i) edit.Created(copies[i]);
        if (!copied || copies.Size() != originals.Size()) return edit.Fail("CKContext::CopyObjects failed");
        for (int i = 0; i < copies.Size(); ++i) {
            if (!MapCopy(BehaviorOf(context, originals[i]), BehaviorOf(context, copies[i]), map))
                return edit.Fail("The copy does not match its original");
            copiedTops.push_back(copies[i]);
        }
    }
    std::set<CK_ID> copiedIds;
    for (const auto &entry : map) copiedIds.insert(entry.second);

    // Attach the copies.
    for (CK_ID id : copiedTops)
        if (!OwnerAccepts(graph, BehaviorOf(context, id)))
            return edit.Fail(std::string(NameOf(context->GetObject(id))) +
                             " does not apply to this Script owner's class without its Target");
    for (CK_ID id : copiedTops) {
        const int at = graph->GetSubBehaviorCount();
        if (!edit.Do([context, graphId, id, at] { return AttachSubBehavior(context, graphId, id, at); },
                     [context, graphId, id] { return DetachSubBehavior(context, graphId, id); }))
            return edit.Fail("CKBehavior::AddSubBehavior failed");
    }

    // Display records of the copied subtrees, level by level.
    std::set<CK_ID> originalsInCopy;
    for (const auto &entry : map) originalsInCopy.insert(entry.first);
    const BehaviorData *target = FindRecord(edit.Data(), graphId);
    const int baseDepth = target == &edit.Data().rootBehavior ? 1 : static_cast<int>(target->depth) + 1;
    std::vector<BehaviorData> added;
    for (const BehaviorData *topRecord : tops) {
        const int shift = baseDepth - static_cast<int>(topRecord->depth);
        for (const auto &record : source.behaviors) {
            bool inside = record.id == topRecord->id;
            for (CK_ID parent = record.parentId; parent && !inside;) {
                inside = parent == topRecord->id;
                const BehaviorData *up = FindRecord(source, parent);
                parent = up ? up->parentId : 0;
            }
            if (!inside) continue;
            BehaviorData copy = record;
            copy.id = Mapped(map, record.id);
            copy.parentId = record.id == topRecord->id ? graphId : Mapped(map, record.parentId);
            copy.depth = static_cast<CKDWORD>(static_cast<int>(record.depth) + shift);
            copy.rect.hPos += dh;
            copy.rect.vPos += dv;
            for (auto &link : copy.links) {
                link.id = Mapped(map, link.id);
                RemapEndpoint(map, link.start);
                RemapEndpoint(map, link.end);
                for (auto &point : link.points) point = Point(point.h + dh, point.v + dv);
            }
            for (auto &operation : copy.operations) {
                operation.id = Mapped(map, operation.id);
                operation.hPos += dh;
                operation.vPos += dv;
            }
            for (auto &comment : copy.comments) {
                comment.hPos += dh;
                comment.vPos += dv;
            }
            for (auto &parameter : copy.localParams) parameter.id = Mapped(map, parameter.id);
            for (auto &parameter : copy.sharedParams) {
                parameter.id = Mapped(map, parameter.id);
                parameter.sourceId = Mapped(map, parameter.sourceId);
            }
            added.push_back(copy);
        }
    }
    auto &records = edit.Data().behaviors;
    for (const auto &record : added) {
        auto level = std::find_if(records.begin(), records.end(),
                                  [&](const BehaviorData &other) { return other.depth > record.depth; });
        records.insert(level, record);
    }
    edit.Data().behaviorCount = static_cast<int>(records.size());

    // Behavior links among the top-level originals become links of the copies.
    std::set<CK_ID> topOriginals;
    for (const BehaviorData *record : tops) topOriginals.insert(record->id);
    for (const BehaviorData *topRecord : tops) {
        const BehaviorData *parentRecord = FindRecord(source, topRecord->parentId);
        CKBehavior *parent = BehaviorOf(context, topRecord->parentId);
        if (!parentRecord || !parent) continue;
        for (const auto &link : parentRecord->links) {
            if (!link.IsBehaviorLink()) continue;
            if (!topOriginals.count(link.start.id) || !topOriginals.count(link.end.id)) continue;
            if (link.start.id != topRecord->id) continue; // each link once, from its start owner
            CKBehaviorLink *original = CKBehaviorLink::Cast(context->GetObject(link.id));
            if (!original) continue;
            CKBehaviorIO *in = CKBehaviorIO::Cast(context->GetObject(Mapped(map, original->GetInBehaviorIO()->GetID())));
            CKBehaviorIO *out = CKBehaviorIO::Cast(context->GetObject(Mapped(map, original->GetOutBehaviorIO()->GetID())));
            CKBehaviorLink *copy = CKBehaviorLink::Cast(context->CreateObject(CKCID_BEHAVIORLINK));
            if (!copy || !in || !out) continue;
            copy->SetInitialActivationDelay(original->GetInitialActivationDelay());
            copy->SetActivationDelay(original->GetInitialActivationDelay());
            const CK_ID id = copy->GetID();
            edit.Created(id);
            const IoRef inRef = IoRefOf(in), outRef = IoRefOf(out);
            const int at = graph->GetSubBehaviorLinkCount();
            if (!edit.Do([context, graphId, id, at, inRef, outRef] {
                    return AttachLink(context, graphId, id, at, inRef, outRef);
                }, [context, graphId, id] { return DetachLink(context, graphId, id); }))
                return edit.Fail("CKBehavior::AddSubBehaviorLink failed");
            Link display = link;
            display.id = id;
            RemapEndpoint(map, display.start);
            RemapEndpoint(map, display.end);
            for (auto &point : display.points) point = Point(point.h + dh, point.v + dv);
            FindRecord(edit.Data(), graphId)->links.push_back(display);
        }
    }

    // Inputs of the top-level copies: a source inside the copy is kept (a
    // parameter link among the pasted behaviors, also displayed); any other
    // source is replaced by a value local holding the current value.
    std::set<std::string> parameterLinks;
    for (const BehaviorData *topRecord : tops) {
        const BehaviorData *parentRecord = FindRecord(source, topRecord->parentId);
        if (!parentRecord) continue;
        for (const auto &link : parentRecord->links) {
            if (link.IsBehaviorLink() || link.end.id != topRecord->id) continue;
            if (!topOriginals.count(link.start.id) || link.start.type != ENDPOINT_POUT) continue;
            Link display = link;
            RemapEndpoint(map, display.start);
            RemapEndpoint(map, display.end);
            for (auto &point : display.points) point = Point(point.h + dh, point.v + dv);
            FindRecord(edit.Data(), graphId)->links.push_back(display);
        }
    }
    for (size_t t = 0; t < tops.size(); ++t) {
        CKBehavior *copy = BehaviorOf(context, copiedTops[t]);
        ValueBytes values;
        for (int i = 0; i < copy->GetInputParameterCount(); ++i) {
            CKParameterIn *input = copy->GetInputParameter(i);
            if (!input) continue;
            CKParameter *direct = input->GetDirectSource();
            CKParameterIn *shared = input->GetSharedSource();
            if (direct && copiedIds.count(direct->GetID())) continue;
            if (direct && map.count(direct->GetID())) {
                // CopyObjects kept a source that was copied too: point to the copy.
                const InputSource before = SourceOf(input);
                const CK_ID to = map[direct->GetID()];
                const InputRef ref = before.input;
                if (!edit.Do([context, ref, to] {
                        CKParameterIn *in = Resolve(context, ref);
                        CKParameter *from = CKParameter::Cast(context->GetObject(to));
                        return in && from && in->SetDirectSource(from) == CK_OK;
                    }, [context, before] { return RestoreSource(context, before); }))
                    return edit.Fail("Cannot connect the pasted parameters");
                continue;
            }
            if (!direct && !shared) continue;
            CKParameter *current = input->GetRealSource();
            if (current) values[i] = current->GetID();
            if (!ClearSource(edit, input)) return edit.Fail("Cannot disconnect a pasted input");
        }
        const BehaviorData *placed = FindRecord(edit.Data(), copiedTops[t]);
        if (!placed || !AddInputValueLocals(edit, graphId, copy, *placed, &values))
            return edit.Fail("Cannot create the input values of the copy");
    }

    // The breakpoints of what was copied (ExtraData by id; a link's owner
    // graph mapped too, the top-level copies' links into the target graph).
    for (const ExtraData &entry : source.extraData) {
        if (!map.count(entry.id1)) continue;
        ExtraData copy = entry;
        copy.id1 = Mapped(map, entry.id1);
        if (entry.id2) copy.id2 = map.count(entry.id2) ? Mapped(map, entry.id2) : graphId;
        if (edit.Data().extraDataVersion == 0) edit.Data().extraDataVersion = source.extraDataVersion ? source.extraDataVersion : 3;
        edit.Data().extraData.push_back(copy);
    }
    BehaviorData *targetRecord = FindRecord(edit.Data(), graphId);
    for (Comment comment : comments) {
        comment.hPos += dh;
        comment.vPos += dv;
        targetRecord->comments.push_back(comment);
    }
    return edit.Commit(copiedTops.empty() ? 0 : copiedTops.front());
}

} // namespace vdev
