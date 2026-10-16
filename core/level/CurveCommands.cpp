// The Curve Setup's commands and the 3D Layout's curve tools (control points).
#include "core/level/LevelCommands.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <set>

#include "core/runtime/CkRuntimeHost.h"
#include "core/runtime/GridCompat.h"
#include "core/graph/RuntimeGraph.h"
#include "core/document/Document.h"
#include "core/graph/ScriptCommands.h"
#include "core/document/UndoHistory.h"
#include "core/level/detail/LevelCommandsDetail.h"

namespace vdev {

using namespace level;

// ---------------------------------------------------------------- Curve points

namespace {

std::vector<CK_ID> PointsOf(CKCurve *curve) {
    std::vector<CK_ID> points;
    for (int i = 0; i < curve->GetControlPointCount(); ++i)
        if (CKCurvePoint *point = curve->GetControlPoint(i)) points.push_back(point->GetID());
    return points;
}

// Gives the curve these control points, in order.
bool SetPoints(Document &doc, CK_ID curveId, const std::vector<CK_ID> &points) {
    CKCurve *curve = Get<CKCurve>(doc, curveId);
    if (!curve) return false;
    curve->RemoveAllControlPoints();
    for (CK_ID id : points)
        if (CKCurvePoint *point = Get<CKCurvePoint>(doc, id)) curve->AddControlPoint(point);
    curve->Update();
    return true;
}

} // namespace

EditResult AddCurvePoint(Document &document, CK_ID curveId, int after) {
    CKContext *context = document.Context();
    CKCurve *curve = Get<CKCurve>(document, curveId);
    if (!curve) return EditResult::Refused("Select a curve");
    CKLevel *level = context->GetCurrentLevel();
    if (!level) return EditResult::Refused("No current level");
    const std::vector<CK_ID> before = PointsOf(curve);
    const int count = static_cast<int>(before.size());
    if (after < 0 || after >= count) after = count - 1;
    // The new point: between the point and the next one, past the last one
    // (one more segment), or at the curve when it has none.
    VxVector position;
    curve->GetPosition(&position);
    CK3dEntity *parent = curve;
    if (count > 0) {
        CKCurvePoint *previous = Get<CKCurvePoint>(document, before[after]);
        VxVector a;
        previous->GetPosition(&a);
        if (previous->GetParent()) parent = previous->GetParent();
        if (after + 1 < count) {
            VxVector b;
            Get<CKCurvePoint>(document, before[after + 1])->GetPosition(&b);
            position = (a + b) * 0.5f;
        } else if (count > 1) {
            VxVector b;
            Get<CKCurvePoint>(document, before[after - 1])->GetPosition(&b);
            position = a + (a - b);
        } else {
            position = a + VxVector(10.0f, 0.0f, 0.0f);
        }
    }
    const std::string name = std::string(curve->GetName() ? curve->GetName() : "Curve") + "_Point" + std::to_string(count + 1);
    CKCurvePoint *point = CKCurvePoint::Cast(context->CreateObject(CKCID_CURVEPOINT, const_cast<CKSTRING>(name.c_str())));
    if (!point) return EditResult::Refused("CKContext::CreateObject failed");
    const CK_ID id = point->GetID(), levelId = level->GetID(), parentId = parent->GetID();
    std::vector<CK_ID> afterPoints = before;
    afterPoints.insert(afterPoints.begin() + (after + 1), id);
    return Apply(
        document, after + 1 < count ? "Insert Point" : "Add Point",
        [=](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKCurvePoint *created = Get<CKCurvePoint>(doc, id);
            if (!owner || !created) return false;
            owner->AddObject(created);
            created->SetParent(Get<CK3dEntity>(doc, parentId), FALSE);
            VxVector at = position;
            created->SetPosition(&at);
            return SetPoints(doc, curveId, afterPoints);
        },
        [=](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            CKCurvePoint *created = Get<CKCurvePoint>(doc, id);
            if (!SetPoints(doc, curveId, before)) return false;
            if (created) {
                created->SetParent(nullptr, TRUE);
                if (owner) owner->RemoveObject(created);
            }
            return true;
        },
        {id}, id);
}

EditResult RemoveCurvePoint(Document &document, CK_ID curveId, int index) {
    CKCurve *curve = Get<CKCurve>(document, curveId);
    if (!curve) return EditResult::Refused("Select a curve");
    CKLevel *level = document.Context()->GetCurrentLevel();
    const std::vector<CK_ID> before = PointsOf(curve);
    if (index < 0 || index >= static_cast<int>(before.size())) return EditResult::Refused("Select a control point");
    const CK_ID id = before[index];
    CKCurvePoint *point = Get<CKCurvePoint>(document, id);
    const CK_ID parentId = point->GetParent() ? point->GetParent()->GetID() : 0;
    const CK_ID levelId = level ? level->GetID() : 0;
    const bool inLevel = level && level->GetLevelScene() && level->GetLevelScene()->IsObjectHere(point);
    std::vector<CK_ID> afterPoints = before;
    afterPoints.erase(afterPoints.begin() + index);
    return Apply(
        document, "Delete Point",
        [=](Document &doc) {
            if (!SetPoints(doc, curveId, afterPoints)) return false;
            if (CKCurvePoint *removed = Get<CKCurvePoint>(doc, id)) {
                removed->SetParent(nullptr, TRUE);
                if (CKLevel *owner = Get<CKLevel>(doc, levelId)) owner->RemoveObject(removed);
            }
            return true;
        },
        [=](Document &doc) {
            if (CKCurvePoint *removed = Get<CKCurvePoint>(doc, id)) {
                if (inLevel)
                    if (CKLevel *owner = Get<CKLevel>(doc, levelId)) owner->AddObject(removed);
                removed->SetParent(parentId ? Get<CK3dEntity>(doc, parentId) : nullptr, TRUE);
            }
            return SetPoints(doc, curveId, before);
        },
        {}, 0, {id});
}

namespace {

// Dev.exe sub_4AC040.
void FitCurve(CKCurve *curve) {
    const int count = curve->GetControlPointCount();
    if (count <= 0) return;
    VxVector scale;
    curve->GetScale(&scale, TRUE);
    auto inverse = [](float v) { return std::fabs(v) > 1.0e-6f ? 0.4f / v : 0.4f; };
    const VxVector pointScale(inverse(scale.x), inverse(scale.y), inverse(scale.z));
    VxVector center(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < count; ++i) {
        CKCurvePoint *point = curve->GetControlPoint(i);
        if (!point) continue;
        point->SetPickable(TRUE);
        point->SetScale(&pointScale, FALSE, TRUE);
        point->ModifyObjectFlags(CK_OBJECT_INTERFACEOBJ, 0);
        VxVector at;
        point->GetPosition(&at);
        center += at;
    }
    center /= static_cast<float>(count);
    curve->SetPosition(&center, nullptr, TRUE);
    curve->Update();
}

using Poses = std::vector<std::pair<CK_ID, VxMatrix>>;

// The curve first: setting its matrix moves its children, set after it.
Poses PosesOf(Document &document, CK_ID curve, const std::vector<CK_ID> &points) {
    Poses poses;
    if (CK3dEntity *entity = Get<CK3dEntity>(document, curve)) poses.emplace_back(curve, entity->GetWorldMatrix());
    for (CK_ID id : points)
        if (CK3dEntity *entity = Get<CK3dEntity>(document, id)) poses.emplace_back(id, entity->GetWorldMatrix());
    return poses;
}

void Place(Document &document, const Poses &poses) {
    for (const auto &pose : poses)
        if (CK3dEntity *entity = Get<CK3dEntity>(document, pose.first)) entity->SetWorldMatrix(pose.second);
}

EditResult CurveTool(Document &document, CK_ID curveId, const std::vector<CK_ID> &points,
                     CK_ID created, CK_ID removed, const std::string &label) {
    CKContext *context = document.Context();
    CKCurve *curve = Get<CKCurve>(document, curveId);
    CKLevel *level = context->GetCurrentLevel();
    if (!curve || !level || !level->GetLevelScene()) return EditResult::Refused("Select a curve");
    const std::vector<CK_ID> before = PointsOf(curve);
    std::vector<CK_ID> touched = before;
    for (CK_ID id : points)
        if (std::find(touched.begin(), touched.end(), id) == touched.end()) touched.push_back(id);
    const Poses posesBefore = PosesOf(document, curveId, touched);
    CKCurvePoint *gone = Get<CKCurvePoint>(document, removed);
    const bool removedInLevel = gone && level->GetLevelScene()->IsObjectHere(gone);
    const CK_ID removedParent = gone && gone->GetParent() ? gone->GetParent()->GetID() : 0;
    // Done now, to know where the curve and its points end up.
    bool ok = false;
    GuardedCall(
        [&] {
            if (CKCurvePoint *made = Get<CKCurvePoint>(document, created)) level->AddObject(made);
            ok = SetPoints(document, curveId, points);
            FitCurve(curve);
        },
        &document.Host(), label.c_str());
    if (!ok) {
        SetPoints(document, curveId, before);
        Place(document, posesBefore);
        if (CKCurvePoint *made = Get<CKCurvePoint>(document, created)) level->RemoveObject(made);
        return EditResult::Refused(label + " failed");
    }
    const Poses posesAfter = PosesOf(document, curveId, touched);
    const CK_ID levelId = level->GetID();
    std::set<CK_ID> made, buried;
    if (created) made.insert(created);
    if (gone) buried.insert(removed);
    return Apply(
        document, label,
        [=](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            if (!owner || !owner->GetLevelScene()) return false;
            if (CKCurvePoint *point = Get<CKCurvePoint>(doc, created))
                if (!owner->GetLevelScene()->IsObjectHere(point)) owner->AddObject(point);
            if (!SetPoints(doc, curveId, points)) return false;
            Place(doc, posesAfter);
            if (CKCurvePoint *point = Get<CKCurvePoint>(doc, removed)) {
                point->SetParent(nullptr, TRUE);
                owner->RemoveObject(point);
            }
            if (CKCurve *edited = Get<CKCurve>(doc, curveId)) edited->Update();
            return true;
        },
        [=](Document &doc) {
            CKLevel *owner = Get<CKLevel>(doc, levelId);
            if (CKCurvePoint *point = Get<CKCurvePoint>(doc, removed)) {
                if (removedInLevel && owner) owner->AddObject(point);
                point->SetParent(removedParent ? Get<CK3dEntity>(doc, removedParent) : nullptr, TRUE);
            }
            if (!SetPoints(doc, curveId, before)) return false;
            Place(doc, posesBefore);
            if (CKCurvePoint *point = Get<CKCurvePoint>(doc, created))
                if (owner) owner->RemoveObject(point);
            if (CKCurve *edited = Get<CKCurve>(doc, curveId)) edited->Update();
            return true;
        },
        made, created, buried);
}

} // namespace

EditResult AddCurvePointAt(Document &document, CK_ID curveId, int index, const VxVector &position,
                           bool linear, const std::string &label) {
    CKContext *context = document.Context();
    CKCurve *curve = Get<CKCurve>(document, curveId);
    if (!curve) return EditResult::Refused("Select a curve");
    std::vector<CK_ID> points = PointsOf(curve);
    const int count = static_cast<int>(points.size());
    char name[512];
    std::snprintf(name, sizeof(name), "%s_Point%04d", curve->GetName() ? curve->GetName() : "", count + 1);
    CKCurvePoint *point = CKCurvePoint::Cast(context->CreateObject(CKCID_CURVEPOINT, name));
    if (!point) return EditResult::Refused("CKContext::CreateObject failed");
    // The parent of its neighbour, else the curve (as AddCurvePoint).
    CK3dEntity *parent = curve;
    if (count > 0) {
        CKCurvePoint *neighbour = Get<CKCurvePoint>(document, points[std::clamp(index, 0, count - 1)]);
        if (neighbour && neighbour->GetParent()) parent = neighbour->GetParent();
    }
    point->SetParent(parent, FALSE);
    VxVector at = position;
    point->SetPosition(&at);
    point->SetLinear(linear ? TRUE : FALSE);
    points.insert(points.begin() + std::clamp(index, 0, count), point->GetID());
    const EditResult result = CurveTool(document, curveId, points, point->GetID(), 0, label);
    if (!result.ok) context->DestroyObject(point);
    return result;
}

EditResult EditCurvePoints(Document &document, CK_ID curve, const std::vector<CK_ID> &points,
                           CK_ID removed, const std::string &label) {
    return CurveTool(document, curve, points, 0, removed, label);
}

} // namespace vdev
