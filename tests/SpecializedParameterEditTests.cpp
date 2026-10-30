#include "application/EditorApplication.h"
#include "core/document/Document.h"
#include "core/graph/ParameterValues.h"
#include "features/values/ParameterCommands.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vdev {
namespace {
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
ParameterValueDescription Read(Document &document, CKParameter *parameter) {
    ParameterValueDescription value; Check(DescribeParameterValue(document, parameter, value), "describe specialized parameter"); return value;
}
CK2dCurve *CurveOf(CKParameter *parameter) { CK2dCurve *curve = nullptr; parameter->GetValue(&curve, FALSE); return curve; }
std::vector<unsigned char> CurveBytes(CKParameter *parameter) {
    auto *chunk = CurveOf(parameter)->Dump(); Check(chunk != nullptr, "dump native curve");
    std::vector<unsigned char> bytes(size_t(chunk->ConvertToBuffer(nullptr))); chunk->ConvertToBuffer(bytes.data());
    DeleteCKStateChunk(chunk); return bytes;
}
class CurveFixture : public CK2dCurve {
  public:
    CurveFixture() {
        FittingCoef = .37f; AddControlPoint(Vx2DVector(.4f, .65f));
        auto *point = GetControlPoint(1); point->UseTCB(FALSE); point->SetInTangent(Vx2DVector(.2f, .6f)); point->SetOutTangent(Vx2DVector(.3f, -.2f));
        GetControlPoint(0)->SetTension(.25f); GetControlPoint(2)->SetBias(-.1f); Update();
    }
};
CK_PARAMETERCOPYFUNCTION dependencyCopy = nullptr;
int dependencyFailure = 0;
void CopyDependencies(CKParameter *target, CKParameter *source) {
    if (dependencyFailure == 2) { dependencyFailure = 0; return; }
    dependencyCopy(target, source);
    if (dependencyFailure) { dependencyFailure = 0; throw std::runtime_error("Dependencies failed after copying"); }
}
}
void TestSpecializedParameterEdits(Document &document, EditorApplication &app, EditorScope scope) {
    auto *context = document.Context(); auto *manager = context->GetParameterManager();
    const auto saved = [&] { document.History().Clear(); document.MarkSaved(); document.History().MarkSavePoint(); };
    const auto count = [&] { return document.History().UndoCount(); };
    const auto submit = [&](CKParameter *parameter, const char *property, const ParameterValue &value, bool commit = true) {
        EditorEdit edit = 0;
        Check(app.BeginEdit(property, scope, document.Reference(parameter->GetID()), edit) == EDITOR_OK, "begin specialized edit");
        const std::vector<EditorField> fields{ParameterValueField(value)}; const auto borrowed = BorrowEditorFields(fields);
        Check(app.UpdateEdit(edit, borrowed.values.front()) == EDITOR_OK, "update specialized edit");
        const auto status = app.FinishEdit(edit, commit);
        if (status == EDITOR_QUEUED) app.EndFrame();
        return status;
    };
    auto *curve = context->CreateCKParameterLocal("Specialized curve", CKPGUID_2DCURVE); Check(curve, "create curve value");
    *CurveOf(curve) = CurveFixture();
    const auto before = Read(document, curve); const auto originalBytes = CurveBytes(curve);
    const auto originalLength = CurveOf(curve)->GetLength();
    Vx2DVector originalPosition; Check(CurveOf(curve)->GetPos(.43f, &originalPosition) == CK_OK, "sample native curve position");
    Check(before.value.kind == ParameterValueKind::Curve2D && before.value.curve.points.size() == 3 &&
          before.value.curve.fitting == double(.37f) && (before.value.curve.points[1].flags & CK2DCURVEPOINT_USETANGENTS),
          "curve description lost fitting or explicit tangents");
    saved(); EditorEdit edit = 0;
    Check(app.BeginEdit("parameter.curve2d", scope, document.Reference(curve->GetID()), edit) == EDITOR_OK, "begin curve drag");
    for (unsigned i = 0; i < 40; ++i) {
        auto value = before.value; value.curve.points[1].values[1] = .3 + .01 * i;
        const std::vector<EditorField> fields{ParameterValueField(value)}; const auto borrowed = BorrowEditorFields(fields);
        Check(app.UpdateEdit(edit, borrowed.values.front()) == EDITOR_OK && CurveBytes(curve) == originalBytes && count() == 0,
              "curve drag wrote the document before acceptance");
    }
    Check(app.FinishEdit(edit, true) == EDITOR_QUEUED, "accept curve drag"); app.EndFrame();
    const auto after = Read(document, curve); const auto afterBytes = CurveBytes(curve);
    Check(count() == 1 && after.value.curve.points[1].values[1] == double(float(.69)) && after.value.curve.fitting == before.value.curve.fitting,
          "curve drag did not commit once or preserve fitting");
    for (unsigned i = 0; i < 4; ++i) {
        Check(document.History().Undo() && CurveBytes(curve) == originalBytes && CurveOf(curve)->GetLength() == originalLength, "curve undo changed stored spline data");
        Vx2DVector position; Check(CurveOf(curve)->GetPos(.43f, &position) == CK_OK && position.x == originalPosition.x && position.y == originalPosition.y,
            "curve undo changed the derived evaluation data");
        Check(document.History().Redo() && CurveBytes(curve) == afterBytes, "curve redo drifted tangent data");
    }
    Check(document.History().Undo(), "prepare curve cancellation");
    auto cancelled = before.value; cancelled.curve.points[0].values[1] = .9;
    Check(submit(curve, "parameter.curve2d", cancelled, false) == EDITOR_CANCELLED && CurveBytes(curve) == originalBytes &&
          !document.Dirty() && document.History().CanRedo() && count() == 0, "curve cancellation discarded values or history");
    Check(submit(curve, "parameter.curve2d", before.value) == EDITOR_QUEUED && count() == 0 && document.History().CanRedo(), "unchanged curve cleared redo");
    auto invalid = before.value; invalid.curve.points[1].values[0] = -1;
    Check(!SetParameterValue(document, document.Reference(curve->GetID()), before.type, invalid).ok && CurveBytes(curve) == originalBytes, "unsorted curve applied partially");
    invalid = before.value; invalid.curve.points[0].values[3] = std::numeric_limits<double>::infinity();
    Check(!SetParameterValue(document, document.Reference(curve->GetID()), before.type, invalid).ok, "nonfinite curve accepted");
    invalid = before.value; invalid.curve.points.resize(1);
    Check(!SetParameterValue(document, document.Reference(curve->GetID()), before.type, invalid).ok, "normal curve lost required points");
    invalid = before.value; invalid.curve.points.resize(4097);
    Check(!SetParameterValue(document, document.Reference(curve->GetID()), before.type, invalid).ok, "unbounded curve accepted");
    const std::vector<EditorField> fields{ParameterValueField(before.value)}; const auto borrowed = BorrowEditorFields(fields);
    const EditorRequest request{scope, borrowed.data(), 1}; const auto preview = app.Query("parameter.curve2d-preview", request);
    Check(preview.status == EDITOR_OK && preview.fields[0].reals.size() == 101, "read curve preview");
    for (unsigned i = 0; i <= 100; ++i) Check(preview.fields[0].reals[i] == double(CurveOf(curve)->GetY(float(i) / 100)), "preview differs from SDK evaluation");
    Check(CurveBytes(curve) == originalBytes, "preview query changed source curve");
    auto *copy = context->CreateCKParameterLocal("Curve copy", CKPGUID_2DCURVE); Check(copy, "create curve copy");
    const auto initialCopy = CurveBytes(copy); saved();
    Check(CopyParameterValue(document, document.Reference(copy->GetID()), document.Reference(curve->GetID())).ok && CurveBytes(copy) == originalBytes,
          "native curve copy recomputed spline data");
    Check(document.History().Undo() && CurveBytes(copy) == initialCopy, "native curve copy undo failed");
    while (CurveOf(copy)->GetControlPointCount()) CurveOf(copy)->DeleteControlPoint(CurveOf(copy)->GetControlPoint(0));
    Check(CopyParameterValue(document, document.Reference(curve->GetID()), document.Reference(copy->GetID())).ok &&
          CurveOf(curve)->GetControlPointCount() == 0 && document.History().Undo() && CurveBytes(curve) == originalBytes,
          "copying an empty legacy curve lost its native behavior");
    document.History().Clear(); context->DestroyObject(copy);
    // All four operations keep unlisted classes and option bits when the mode changes.
    std::vector<CKParameter *> parameters{curve};
    for (auto type : {CKPGUID_COPYDEPENDENCIES, CKPGUID_DELETEDEPENDENCIES, CKPGUID_SAVEDEPENDENCIES, CKPGUID_REPLACEDEPENDENCIES}) {
        auto *parameter = context->CreateCKParameterLocal("Specialized dependencies", type); Check(parameter, "create dependency value"); parameters.push_back(parameter);
        CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_CUSTOM; dependencies.Resize(CKGetClassCount() + 3); dependencies.Fill(0);
        dependencies[CKCID_OBJECT] = 0x80000001; dependencies[dependencies.Size() - 1] = 0xabcdef01;
        Check(SetHeldValue(parameter, &dependencies), "initialize dependency value");
        const auto original = Read(document, parameter);
        Check(original.value.kind == ParameterValueKind::Dependencies, "dependency value kind missing");
        if (type == CKPGUID_COPYDEPENDENCIES || type == CKPGUID_DELETEDEPENDENCIES)
            Check(!original.dependencyClasses.empty(), "copy/delete dependency options metadata missing");
        auto changed = original.value; changed.integer = CK_DEPENDENCIES_NONE; saved();
        Check(submit(parameter, "parameter.dependencies", changed) == EDITOR_QUEUED && count() == 1 && Read(document, parameter).value == changed &&
              document.History().Undo() && Read(document, parameter).value == original.value && document.History().CanRedo(), "dependency mode discarded hidden options");
        Check(submit(parameter, "parameter.dependencies", changed, false) == EDITOR_CANCELLED && !document.Dirty() && count() == 0 && document.History().CanRedo(), "dependency cancel changed history");
        auto invalid = original.value; invalid.integer = 3;
        Check(!SetParameterValue(document, document.Reference(parameter->GetID()), type, invalid).ok, "unknown dependency mode accepted");
    }
    // A throwing copy must release its temporary carrier and compensate the value.
    auto *parameter = parameters[1]; auto *working = context->CreateCKParameterOut("Dependency working copy", parameter->GetGUID(), TRUE);
    Check(working && working->CopyValue(parameter, FALSE) == CK_OK, "create dependency working copy");
    const auto original = Read(document, working); auto changed = original.value; changed.integer = CK_DEPENDENCIES_FULL;
    auto *description = manager->GetParameterTypeDescription(parameter->GetType());
    {
        dependencyCopy = description->CopyFunction;
        struct Restore { CKParameterTypeDesc *type; ~Restore() { type->CopyFunction = dependencyCopy; dependencyFailure = 0; } } restore{description};
        description->CopyFunction = CopyDependencies;
        for (int failure : {1, 2}) {
            const auto objects = context->GetObjectsCountByClassID(CKCID_PARAMETEROUT); dependencyFailure = failure;
            Check(!WriteParameterDraft(document, working, changed) && Read(document, working).value == original.value &&
                  context->GetObjectsCountByClassID(CKCID_PARAMETEROUT) == objects, "failed dependency copy leaked a carrier or changed draft");
        }
    }
    context->DestroyObject(working);
    // LOD uses the actual loaded plugin type, including its hidden proportions.
    const CKGUID lodType(0x2b2d2f43, 0x02ab7bc5);
    if (manager->ParameterGuidToType(lodType) >= 0) {
        auto *lod = context->CreateCKParameterLocal("Specialized LOD", lodType); Check(lod, "create LOD value"); parameters.push_back(lod);
        const auto original = Read(document, lod); auto changed = original.value; changed.integer = int32_t(0x80000001); changed.components[4] = .23;
        saved(); Check(submit(lod, "parameter.lod-options", changed) == EDITOR_QUEUED && count() == 1, "LOD edit not committed once");
        Check(Read(document, lod).value.integer == changed.integer && Read(document, lod).value.components[4] == double(float(.23)), "LOD masks or proportions lost");
        Check(document.History().Undo() && Read(document, lod).value == original.value && !document.Dirty(), "LOD undo lost hidden proportions");
    } else Check(false, "LOD plugin type is required for specialized-value regressions");
    // Nested values are one parent record and retain each native parameter member.
    const CKGUID nestedType(0x6e76616c, 0x00000450); XArray<CKGUID> members;
    for (auto type : {CKPGUID_2DCURVE, CKPGUID_COPYDEPENDENCIES, lodType}) members.PushBack(type);
    Check(manager->RegisterNewStructure(nestedType, "Specialized values", "Curve,Dependencies,LOD", members) == CK_OK, "register specialized structure");
    auto *nested = context->CreateCKParameterLocal("Nested specialized values", nestedType); Check(nested, "create specialized structure");
    const auto previous = Read(document, nested); auto next = previous.value;
    next.members[0].curve.points[0].values[1] = .25; next.members[1].integer = CK_DEPENDENCIES_FULL; next.members[2].integer |= 1;
    saved(); Check(submit(nested, "parameter.structure", next) == EDITOR_QUEUED && count() == 1, "nested specialized values split history");
    Check(document.History().Undo() && Read(document, nested).value == previous.value && Read(document, nested).members[0].parameter == previous.members[0].parameter,
          "nested specialized undo replaced native members");
    document.History().Clear(); context->DestroyObject(nested); manager->UnRegisterParameterType(nestedType);
    for (auto *object : parameters) context->DestroyObject(object);
}
}
