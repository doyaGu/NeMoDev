#include "features/layout/LayoutGestures.h"
#include "application/ObjectSelection.h"
#include "core/document/Document.h"
#include "core/layout/LayoutTools.h"
#include "core/layout/LayoutView.h"
#include "core/layout/LayoutCommands.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <set>

namespace vdev {
namespace {
const EditorValue *Arg(const EditorRequest &r, const char *key) {
    for (uint32_t i = 0; i < r.count; ++i)
        if (std::string(r.values[i].key.data, r.values[i].key.size) == key)
            return r.values + i;
    return nullptr;
}
uint64_t Number(const EditorRequest &r, const char *key, uint64_t fallback = 0) {
    const auto *v = Arg(r, key);
    return v ? v->integer : fallback;
}
ObjectRef Object(const EditorRequest &r, const char *key) {
    const auto *v = Arg(r, key);
    return v ? v->object : ObjectRef{};
}
EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_OK;
}
EditorStatus Result(const EditResult &r, EditorSink sink) {
    Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(r.message)}});
    return r.ok                                ? EDITOR_OK
           : r.Unchanged()                     ? EDITOR_NO_CHANGE
           : r.Status() == EditStatus::Refused ? EDITOR_REFUSED
                                               : EDITOR_ERROR;
}
bool Vector(const EditorValue *v, size_t size, std::vector<float> &out) {
    if (!v || v->type != EDITOR_REAL_LIST || v->reals.count != size)
        return false;
    for (size_t i = 0; i < size; ++i) {
        const double n = v->reals.data[i];
        if (!std::isfinite(n) || std::abs(n) > float(INT_MAX / 4))
            return false;
        out.push_back(float(n));
    }
    return true;
}
struct Options {
    CameraSteps camera;
    TransformSettings settings;
    TransformSteps transform;
    Snap2D snapping;
    FlySteps fly;
    int scaleMode = 0;
    bool shift = false, ctrl = false, locked = false;
};
// Configuration records use named fields. Unknown fields and wrong value types
// fail before any native object is touched.
bool Configure(const EditorValue *value, Options &o) {
    if (!value || value->type != EDITOR_RECORD)
        return false;
    std::set<std::string> seen;
    for (uint32_t i = 0; i < value->fields.count; ++i) {
        const auto &v = value->fields.data[i];
        const std::string key(v.key.data, v.key.size);
        if (!seen.insert(key).second)
            return false;
        float *real = nullptr;
        bool *boolean = nullptr;
        int *integer = nullptr;
        if (key == "camera-rotate")
            real = &o.camera.rotate;
        if (key == "camera-translate")
            real = &o.camera.translate;
        if (key == "camera-zoom")
            real = &o.camera.zoom;
        if (key == "camera-ortho-zoom")
            real = &o.camera.orthoZoom;
        if (key == "camera-roll")
            real = &o.camera.roll;
        if (key == "camera-fov")
            real = &o.camera.fov;
        if (key == "free-orbit-value")
            real = &o.camera.freeOrbitValue;
        if (key == "object-translate")
            real = &o.transform.translate;
        if (key == "object-rotate")
            real = &o.transform.rotate;
        if (key == "object-scale")
            real = &o.transform.scale;
        if (key == "snap-angle")
            real = &o.transform.snapAngle;
        if (key == "snap-scale")
            real = &o.transform.snapScale;
        if (key == "fly-turn")
            real = &o.fly.turn;
        if (key == "fly-advance")
            real = &o.fly.advance;
        if (key == "camera-rotate-constant")
            boolean = &o.camera.rotateConstant;
        if (key == "camera-translate-constant")
            boolean = &o.camera.translateConstant;
        if (key == "camera-zoom-constant")
            boolean = &o.camera.zoomConstant;
        if (key == "camera-ortho-zoom-constant")
            boolean = &o.camera.orthoZoomConstant;
        if (key == "camera-roll-constant")
            boolean = &o.camera.rollConstant;
        if (key == "camera-fov-constant")
            boolean = &o.camera.fovConstant;
        if (key == "translate-sticky")
            boolean = &o.transform.translateSticky;
        if (key == "rotate-sticky")
            boolean = &o.transform.rotateSticky;
        if (key == "scale-sticky")
            boolean = &o.transform.scaleSticky;
        if (key == "hierarchy")
            boolean = &o.settings.hierarchy;
        if (key == "snap")
            boolean = &o.settings.snap;
        if (key == "homogenous")
            boolean = &o.snapping.homogenous;
        if (key == "fly-constant")
            boolean = &o.fly.constant;
        if (key == "shift")
            boolean = &o.shift;
        if (key == "ctrl")
            boolean = &o.ctrl;
        if (key == "locked")
            boolean = &o.locked;
        if (key == "free-orbit-type")
            integer = &o.camera.freeOrbitType;
        if (key == "orbit-selection-type")
            integer = &o.camera.orbitSelectionType;
        if (key == "axes")
            integer = &o.settings.axes;
        if (key == "referential")
            integer = &o.settings.referential;
        if (key == "pivot")
            integer = &o.settings.pivot;
        if (key == "scale-mode")
            integer = &o.scaleMode;
        if (key == "snap-mode")
            integer = &o.transform.snapMode;
        if (key == "snap-radius")
            integer = &o.transform.snapRadius;
        if (key == "snap-influence")
            integer = &o.snapping.influence;
        if (real) {
            if (v.type != EDITOR_REAL_VALUE || !std::isfinite(v.real) || std::abs(v.real) > 100000000.f)
                return false;
            *real = float(v.real);
        } else if (boolean) {
            if (v.type != EDITOR_INTEGER_VALUE || v.integer > 1)
                return false;
            *boolean = v.integer != 0;
        } else if (integer) {
            if (v.type != EDITOR_INTEGER_VALUE || v.integer > INT_MAX)
                return false;
            *integer = int(v.integer);
        } else {
            std::vector<float> f;
            if (key == "world-min" || key == "world-max" || key == "world-step") {
                if (!Vector(&v, 3, f))
                    return false;
                auto vector = VxVector(f[0], f[1], f[2]);
                if (key == "world-min")
                    o.camera.worldMin = o.fly.worldMin = vector;
                else if (key == "world-max")
                    o.camera.worldMax = o.fly.worldMax = vector;
                else
                    o.transform.worldStep = vector;
            } else if (key == "screen-step" || key == "screen-offset") {
                if (!Vector(&v, 2, f))
                    return false;
                auto *to = key == "screen-step" ? o.snapping.screenStep : o.snapping.screenOffset;
                to[0] = f[0];
                to[1] = f[1];
            } else
                return false;
        }
    }
    o.snapping.mode = o.transform.snapMode;
    return o.settings.axes >= 0 && o.settings.axes <= 7 && o.settings.referential <= 5 && o.settings.pivot <= 2 &&
           o.scaleMode <= 2 && o.camera.freeOrbitType <= 2 && o.camera.orbitSelectionType <= 2;
}
struct Motion {
    int x = 0, y = 0;
    bool shift = false, ctrl = false, move = false, idle = false;
    unsigned buttons = 0;
    double time = 0;
};
bool ReadMotion(const EditorValue *value, Motion &m) {
    if (!value || value->type != EDITOR_RECORD)
        return false;
    EditorRequest r{{}, value->fields.data, value->fields.count};
    std::set<std::string> seen;
    for (uint32_t i = 0; i < r.count; ++i) {
        const auto &v = r.values[i];
        const std::string key(v.key.data, v.key.size);
        if (!seen.insert(key).second)
            return false;
        if (key == "position") {
            std::vector<float> v2;
            if (!Vector(&v, 2, v2))
                return false;
            m.x = int(v2[0]);
            m.y = int(v2[1]);
        } else if (key == "time") {
            if (v.type != EDITOR_REAL_VALUE || !std::isfinite(v.real) || v.real < 0)
                return false;
            m.time = v.real;
        } else if (key == "shift" || key == "ctrl" || key == "move" || key == "idle" || key == "buttons") {
            if (v.type != EDITOR_INTEGER_VALUE || v.integer > (key == "buttons" ? 3u : 1u))
                return false;
        } else
            return false;
    }
    m.shift = Number(r, "shift") != 0;
    m.ctrl = Number(r, "ctrl") != 0;
    m.move = Number(r, "move") != 0;
    m.idle = Number(r, "idle") != 0;
    m.buttons = unsigned(Number(r, "buttons"));
    return Arg(r, "position") != nullptr;
}
int AllowedAxes(LayoutView &view) {
    auto *camera = view.Camera();
    if (!camera)
        return 7;
    const auto id = camera->GetID();
    return id == view.ViewCamera(LayoutViewCamera::Top)     ? 5
           : id == view.ViewCamera(LayoutViewCamera::Front) ? 3
           : id == view.ViewCamera(LayoutViewCamera::Right) ? 6
                                                            : 7;
}
} // namespace
struct LayoutGestures::Data {
    std::function<Document *()> current;
    LayoutTargetLookup view;
    ObjectSelection &selection;
    struct Gesture {
        Data *owner;
        EditorView viewport = 0;
        DocumentToken document{};
        CKContext *context = nullptr;
        ObjectRef initialCamera{}, camera{}, curve{};
        uint64_t revision = 0, topology = 0;
        unsigned kind = 0, action = 0;
        Options options;
        CameraGesture cameraMove;
        TransformGesture transform;
        Transform2DGesture sprite;
        ObjectsGesture flight;
        std::vector<ObjectRef> objects;
        bool finished = false;
        int flyStartX = 0, flyStartY = 0, flyLastX = 0, flyLastY = 0;
        double flyTime = 0;
        unsigned flyButtons = 0;
        FlyMode flyMode = FlyMode::Advance;
        VxVector flyCenter;
        explicit Gesture(Data *owner) : owner(owner) {}
        ~Gesture() {
            if (!finished)
                owner->Cancel(*this);
            if (owner->active == this)
                owner->active = nullptr;
        }
    };
    struct CurveTool {
        DocumentToken document{};
        ObjectRef curve{};
        unsigned tool = 0, plane = 0;
    };
    std::map<EditorView, CurveTool> tools;
    enum Action { Wheel, Zoom, Click, CurveClick, SetCurveTool };
    struct Entry {
        Data *owner;
        Action action;
    } entries[5];
    Gesture *active = nullptr; // borrowed from the command host's active preview
    Data(std::function<Document *()> current, LayoutTargetLookup view, ObjectSelection &selection)
        : current(std::move(current)), view(std::move(view)), selection(selection),
          entries{{this, Wheel}, {this, Zoom}, {this, Click}, {this, CurveClick}, {this, SetCurveTool}} {}
    Document *DocumentOf(const EditorRequest &r) const {
        auto *doc = current();
        return doc && doc->IsOpen() && doc->Token() == r.scope.document ? doc : nullptr;
    }
    bool Current(const Gesture &g) const {
        auto *doc = current();
        const auto target = view(g.viewport);
        if (!target.view) return false;
        auto &v = *target.view;
        return doc && doc->IsOpen() && doc->Token() == g.document && v.Context() == g.context &&
               doc->Context() == g.context && doc->ChangeRevision() == g.revision &&
               doc->TopologyRevision() == g.topology && v.Camera() && doc->Reference(v.Camera()->GetID()) == g.camera &&
               std::all_of(g.objects.begin(), g.objects.end(), [&](ObjectRef o) { return bool(doc->Resolve(o)); }) &&
               (!g.curve || doc->Resolve(g.curve));
    }
    bool Cancel(Gesture &g) {
        if (g.finished)
            return true;
        g.finished = true;
        auto *doc = current();
        const auto target = view(g.viewport);
        if (!doc || doc->Token() != g.document || doc->Context() != g.context ||
            !target.view || target.view->Context() != g.context)
            return true;
        const bool a = g.cameraMove.Cancel(doc), b = g.transform.Cancel(doc), c = g.sprite.Cancel(doc);
        const bool d = !g.flight.Active() || g.flight.Cancel(*doc);
        return a && b && c && d;
    }
    EditorStatus Validate(const EditorRequest &r, Options &o, Motion &m) {
        auto *doc = DocumentOf(r);
        const auto target = view(r.scope.view);
        if (!doc || !target.view || target.view->Context() != doc->Context())
            return EDITOR_STALE;
        if (active) {
            if (active->viewport != target.id)
                return EDITOR_REFUSED;
            if (active->initialCamera != Object(r, "target") || !Current(*active))
                return EDITOR_STALE;
            return ReadMotion(Arg(r, "value"), m) ? EDITOR_OK : EDITOR_INVALID;
        }
        if (!doc->Resolve(Object(r, "target"), CKCID_CAMERA))
            return EDITOR_STALE;
        const auto kind = Number(r, "kind"), action = Number(r, "action");
        if ((Arg(r, "started") && (!std::isfinite(Arg(r, "started")->real) || Arg(r, "started")->real < 0)) ||
            kind > 3 || action > (kind == 0 ? 6u : 2u) || !Configure(Arg(r, "options"), o) ||
            !ReadMotion(Arg(r, "value"), m))
            return EDITOR_INVALID;
        if ((kind == 2 &&
             (action == unsigned(TransformAction::Rotate) || o.settings.axes < 1 || o.settings.axes > 3)) ||
            (kind == 3 && action))
            return EDITOR_INVALID;
        std::vector<float> start;
        if (!Vector(Arg(r, "start"), 2, start))
            return EDITOR_INVALID;
        if (const auto *point = Arg(r, "point"); point && point->reals.count) {
            std::vector<float> p;
            if (!Vector(point, 3, p))
                return EDITOR_INVALID;
        }
        const auto referent = Object(r, "referent"), curve = Object(r, "curve"), grabbed = Object(r, "grabbed");
        if ((referent && !doc->Resolve(referent, CKCID_3DENTITY)) || (curve && !doc->Resolve(curve, CKCID_CURVE)) ||
            (grabbed && !doc->Resolve(grabbed)))
            return EDITOR_STALE;
        const auto *objects = Arg(r, "objects");
        std::set<ObjectRef> unique;
        for (uint32_t i = 0; objects && i < objects->objects.count; ++i) {
            auto ref = objects->objects.data[i];
            if (!doc->Resolve(ref) || !unique.insert(ref).second)
                return EDITOR_STALE;
        }
        return target.view->Camera() &&
                       doc->Reference(target.view->Camera()->GetID()) == Object(r, "target")
                   ? EDITOR_OK
                   : EDITOR_STALE;
    }
    EditorStatus Begin(const EditorRequest &r, std::unique_ptr<Gesture> &g) {
        Options options;
        Motion motion;
        const auto status = Validate(r, options, motion);
        if (status != EDITOR_OK)
            return status;
        if (active)
            return EDITOR_REFUSED;
        auto *doc = DocumentOf(r);
        const auto target = view(r.scope.view);
        auto &v = *target.view;
        g = std::make_unique<Gesture>(this);
        g->viewport = target.id;
        g->document = doc->Token();
        g->context = doc->Context();
        g->revision = doc->ChangeRevision();
        g->topology = doc->TopologyRevision();
        g->initialCamera = Object(r, "target");
        g->curve = Object(r, "curve");
        g->kind = unsigned(Number(r, "kind"));
        g->action = unsigned(Number(r, "action"));
        g->options = options;
        const auto *input = Arg(r, "objects");
        std::vector<CK_ID> ids;
        for (uint32_t i = 0; input && i < input->objects.count; ++i)
            ids.push_back(input->objects.data[i].object);
        std::vector<float> start;
        Vector(Arg(r, "start"), 2, start);
        const int x = int(start[0]), y = int(start[1]);
        std::vector<float> p;
        const auto *point = Arg(r, "point");
        const bool hasPoint = point && point->reals.count && Vector(point, 3, p);
        VxVector grabbed;
        if (hasPoint)
            grabbed = VxVector(p[0], p[1], p[2]);
        options.settings.referent = Object(r, "referent").object;
        bool began = false;
        if (g->kind == 0) {
            began = g->cameraMove.Begin(v, CameraAction(g->action), options.camera, x, y, ids, options.locked,
                                        options.shift, options.ctrl);
            ids = g->cameraMove.Objects();
        } else if (g->kind == 1) {
            began = g->transform.Begin(v, TransformAction(g->action), options.settings, options.transform,
                                       options.scaleMode, ids, x, y, hasPoint ? &grabbed : nullptr);
            ids = g->transform.Objects();
        } else if (g->kind == 2) {
            began =
                g->sprite.Begin(v, TransformAction(g->action), options.settings.axes, options.settings.snap,
                                options.snapping, options.transform.snapScale, ids, Object(r, "grabbed").object, x, y);
            ids = g->sprite.Objects();
        } else {
            auto *camera = v.Camera();
            ids = {camera->GetID()};
            if (auto *target = CKTargetCamera::Cast(camera))
                if (auto *object = target->GetTarget())
                    ids.push_back(object->GetID());
            began = g->flight.Begin(*doc, ids);
            g->flyStartX = g->flyLastX = x;
            g->flyStartY = g->flyLastY = y;
            g->flyTime = Arg(r, "started") ? Arg(r, "started")->real : 0;
        }
        if (!began)
            return EDITOR_REFUSED;
        g->camera = doc->Reference(v.Camera()->GetID());
        g->objects = doc->References(ids);
        if (auto ref = Object(r, "referent"))
            g->objects.push_back(ref);
        auto &tool = tools[target.id];
        if (g->kind == 3 && tool.document == doc->Token())
            tool.tool = 0;
        active = g.get();
        return EDITOR_OK;
    }
    EditorStatus Update(Gesture &g, const EditorRequest &r) {
        if (!Current(g))
            return EDITOR_STALE;
        Motion m;
        if (!ReadMotion(Arg(r, "value"), m))
            return EDITOR_INVALID;
        if (g.kind == 0) {
            if (m.move)
                g.cameraMove.Move(m.x, m.y, m.shift);
            if (m.idle)
                g.cameraMove.Idle(m.shift);
        } else if (g.kind == 1) {
            if (m.move)
                g.transform.Move(m.x, m.y);
        } else if (g.kind == 2) {
            if (m.move)
                g.sprite.Move(m.x, m.y);
        } else {
            auto &v = *view(g.viewport).view;
            if (m.time < g.flyTime)
                return EDITOR_INVALID;
            if (m.buttons != g.flyButtons) {
                const bool left = m.buttons & 1, right = m.buttons & 2, leftPressed = left && !(g.flyButtons & 1);
                auto mode = left && right ? (leftPressed ? FlyMode::Look : FlyMode::Strafe)
                            : left        ? FlyMode::Look
                            : right       ? FlyMode::Strafe
                                          : FlyMode::Advance;
                if (left && right && !m.ctrl) {
                    VxVector point;
                    auto *entity = CK3dEntity::Cast(g.context->GetObject(v.Pick(m.x, m.y, false, &point)));
                    VxBbox box;
                    if (entity && entity->GetCurrentMesh()) {
                        g.flyCenter = point;
                        mode = FlyMode::Orbit;
                    } else if (entity && LayoutBox(entity, box)) {
                        g.flyCenter = box.GetCenter();
                        mode = FlyMode::Orbit;
                    }
                }
                g.flyMode = mode;
                g.flyButtons = m.buttons;
                g.flyStartX = g.flyLastX = m.x;
                g.flyStartY = g.flyLastY = m.y;
                g.flyTime = m.time;
                return EDITOR_OK;
            }
            const int fromX = g.options.fly.constant ? g.flyStartX : g.flyLastX,
                      fromY = g.options.fly.constant ? g.flyStartY : g.flyLastY;
            const float seconds = float(m.time - g.flyTime);
            g.flyTime = m.time;
            g.flyLastX = m.x;
            g.flyLastY = m.y;
            Fly(v, g.flyMode, float(fromX - m.x) / std::max(1, v.Width()), float(fromY - m.y) / std::max(1, v.Height()),
                seconds, g.options.fly, m.shift, g.flyCenter);
        }
        return EDITOR_OK;
    }
    EditorStatus Finish(Gesture &g, EditorSink sink) {
        if (!Current(g))
            return EDITOR_STALE;
        auto *doc = current();
        EditResult result = EditResult::NoChange();
        if (g.kind == 0)
            result = g.cameraMove.Finish(doc);
        else if (g.kind == 1) {
            result = g.transform.Finish(doc);
            if (result.ok && g.curve) {
                auto *curve = CKCurve::Cast(doc->Resolve(g.curve));
                std::vector<CK_ID> points;
                for (int i = 0; i < curve->GetControlPointCount(); ++i)
                    if (auto *p = curve->GetControlPoint(i))
                        points.push_back(p->GetID());
                const auto update = EditCurvePoints(*doc, curve->GetID(), points, 0, "Edit Points");
                if (!update.ok && !update.Unchanged())
                    result = update;
            }
        } else if (g.kind == 2)
            result = g.sprite.Finish(doc);
        else
            result = g.flight.Commit(*doc, "Fly Camera", !view(g.viewport).view->IsViewCamera(g.camera.object));
        g.finished = true;
        return Result(result, sink);
    }
    static EditorStatus Check(void *data, const EditorHost *, const EditorRequest *r) {
        Options o;
        Motion m;
        return static_cast<Data *>(data)->Validate(*r, o, m);
    }
    static EditorStatus Preview(void *data, const EditorHost *, const EditorRequest *r, EditorPreviewAction action,
                                void **state) {
        auto &self = *static_cast<Data *>(data);
        if (action == EDITOR_PREVIEW_CANCEL || action == EDITOR_PREVIEW_RELEASE) {
            std::unique_ptr<Gesture> g(static_cast<Gesture *>(*state));
            *state = nullptr;
            if (!g)
                return EDITOR_OK;
            if (action == EDITOR_PREVIEW_RELEASE) {
                g->finished = true;
                return EDITOR_OK;
            }
            return self.Cancel(*g) ? EDITOR_OK : EDITOR_ERROR;
        }
        if (action != EDITOR_PREVIEW_UPDATE)
            return EDITOR_INVALID;
        if (!*state) {
            std::unique_ptr<Gesture> g;
            const auto status = self.Begin(*r, g);
            if (status != EDITOR_OK)
                return status;
            *state = g.release();
        }
        return self.Update(*static_cast<Gesture *>(*state), *r);
    }
    static EditorStatus Commit(void *data, const EditorHost *, const EditorRequest *, void *state, EditorSink sink) {
        return state ? static_cast<Data *>(data)->Finish(*static_cast<Gesture *>(state), sink) : EDITOR_INVALID;
    }
    static EditorStatus Run(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        std::unique_ptr<Gesture> g;
        auto status = self.Begin(*r, g);
        if (status != EDITOR_OK)
            return status;
        status = self.Update(*g, *r);
        return status == EDITOR_OK ? self.Finish(*g, sink) : status;
    }
    static EditorStatus Tool(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        const auto target = self.view(r->scope.view);
        if (!target.view) return EDITOR_STALE;
        auto &state = self.tools[target.id];
        auto *doc = self.DocumentOf(*r);
        if (!doc)
            return EDITOR_STALE;
        const bool same = state.document == doc->Token();
        const auto curve = same && doc->Resolve(state.curve, CKCID_CURVE) ? state.curve : ObjectRef{};
        return Write(sink, {{"target", EDITOR_OBJECT, curve},
                            {"tool", EDITOR_INTEGER_VALUE, {}, {}, curve ? state.tool : 0},
                            {"plane", EDITOR_INTEGER_VALUE, {}, {}, same ? state.plane : 0}});
    }
    static EditorStatus Act(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &entry = *static_cast<Entry *>(data);
        auto &self = *entry.owner;
        const auto target = self.view(r->scope.view);
        if (!target.view) return EDITOR_STALE;
        auto &state = self.tools[target.id];
        auto *doc = self.DocumentOf(*r);
        if (!doc)
            return EDITOR_STALE;
        auto &view = *target.view;
        auto *context = doc->Context();
        if (entry.action == SetCurveTool) {
            const auto tool = Number(*r, "tool"), plane = Number(*r, "plane");
            auto ref = Object(*r, "target");
            if ((tool && (tool < 128 || tool > 131)) || plane > 1)
                return EDITOR_INVALID;
            if (ref && !doc->Resolve(ref, CKCID_CURVE))
                return EDITOR_STALE;
            if (tool && !ref)
                return EDITOR_INVALID;
            state.document = doc->Token();
            state.curve = ref;
            state.tool = unsigned(tool);
            state.plane = unsigned(plane);
            return EDITOR_OK;
        }
        if (view.Context() != context)
            return EDITOR_STALE;
        if (entry.action == Click) {
            if (Number(*r, "double-click") > 1)
                return EDITOR_INVALID;
            LayoutPickInfo pick;
            pick.entity2d = Object(*r, "sprite").object;
            pick.entity3d = Object(*r, "entity").object;
            pick.character = Object(*r, "character").object;
            if ((pick.entity2d && !doc->Resolve(Object(*r, "sprite"), CKCID_2DENTITY)) ||
                (pick.entity3d && !doc->Resolve(Object(*r, "entity"), CKCID_3DENTITY)) ||
                (pick.character && !doc->Resolve(Object(*r, "character"), CKCID_CHARACTER)))
                return EDITOR_STALE;
            LayoutSendClick(context, pick, Number(*r, "double-click") != 0);
            return EDITOR_OK;
        }
        if (entry.action == Wheel || entry.action == Zoom) {
            Options options;
            if (!Configure(Arg(*r, "options"), options))
                return EDITOR_INVALID;
            if (!view.Camera() || doc->Reference(view.Camera()->GetID()) != Object(*r, "target"))
                return EDITOR_STALE;
            if (entry.action == Wheel) {
                const auto *notches = Arg(*r, "notches"), *extent = Arg(*r, "extent");
                if (!notches || !extent || !std::isfinite(notches->real) || std::abs(notches->real) > 1000000 ||
                    !std::isfinite(extent->real) || extent->real <= 0 || extent->real > 100000000)
                    return EDITOR_INVALID;
                WheelZoom(view, options.camera, int(notches->real), float(extent->real));
                return EDITOR_OK;
            }
            if (Number(*r, "scene") > 1)
                return EDITOR_INVALID;
            std::vector<CK_ID> objects;
            const auto *selected = Arg(*r, "objects");
            for (uint32_t i = 0; selected && i < selected->objects.count; ++i)
                objects.push_back(selected->objects.data[i].object);
            float extent = 5.f;
            if (!ZoomOnObjects(view, objects, Number(*r, "scene") != 0, &extent))
                return Result(EditResult::Refused("Nothing selected"), sink);
            EditorField result{"extent", EDITOR_REAL_VALUE};
            result.real = extent;
            return Write(sink, {result});
        }
        auto *curve = CKCurve::Cast(doc->Resolve(Object(*r, "target")));
        if (!curve)
            return EDITOR_STALE;
        const auto action = Number(*r, "action"), right = Number(*r, "right"), plane = Number(*r, "plane");
        const auto *distance = Arg(*r, "plane-distance");
        std::vector<float> xy;
        if (action > 2 || right > 1 || plane > 1 || !Vector(Arg(*r, "position"), 2, xy) || !distance ||
            !std::isfinite(distance->real) || std::abs(distance->real) > 100000000)
            return EDITOR_INVALID;
        const int x = int(xy[0]), y = int(xy[1]), count = curve->GetControlPointCount();
        const auto curveId = curve->GetID();
        bool linear = count > 0;
        std::vector<CK_ID> points;
        for (int i = 0; i < count; ++i)
            if (auto *point = curve->GetControlPoint(i)) {
                points.push_back(point->GetID());
                if (!point->IsLinear())
                    linear = false;
            }
        const auto pickPoint = [&] {
            const auto pickable = curve->IsPickable();
            struct Restore {
                CKCurve *curve;
                CKBOOL value;
                ~Restore() { curve->SetPickable(value); }
            } restore{curve, pickable};
            curve->SetPickable(FALSE);
            return view.Pick(x, y);
        };
        if (action == 0) {
            if (count > 1 && curve->IsOpen() && !points.empty() && pickPoint() == points.front()) {
                const auto result = EditObjectsState(*doc, {curveId}, "Close Curve", [](CKObject *object) {
                    auto *curve = CKCurve::Cast(object);
                    curve->Close();
                    curve->Update();
                });
                if (result.ok && state.curve == Object(*r, "target"))
                    state.tool = 0;
                return Result(result, sink);
            }
            VxVector point, normal, hit;
            if (!plane) {
                const auto guide = view.GuideMatrix();
                point = VxVector(guide[3][0], guide[3][1], guide[3][2]);
                normal = VxVector(guide[1][0], guide[1][1], guide[1][2]);
            } else {
                auto *camera = view.Camera();
                if (!camera)
                    return EDITOR_REFUSED;
                VxVector up;
                camera->GetOrientation(&normal, &up);
                camera->GetPosition(&point);
                point += normal * float(distance->real);
            }
            if (!view.RayOnPlane(x, y, point, normal, hit))
                return EDITOR_NO_CHANGE;
            return Result(AddCurvePointAt(*doc, curveId, right ? 0 : count, hit, linear, "Add Points"), sink);
        }
        if (action == 1) {
            VxVector hit;
            float along = 0;
            if (!view.CurveAt(x, y, curveId, hit, &along))
                return EDITOR_NO_CHANGE;
            const float distance = along * curve->GetLength();
            int index = 0;
            while (index < count && curve->GetControlPoint(index) &&
                   curve->GetControlPoint(index)->GetLength() <= distance)
                ++index;
            return Result(AddCurvePointAt(*doc, curveId, index, hit, linear, "Insert Points"), sink);
        }
        const auto picked = pickPoint();
        const auto found = std::find(points.begin(), points.end(), picked);
        if (!picked || found == points.end())
            return EDITOR_NO_CHANGE;
        points.erase(found);
        const auto result = EditCurvePoints(*doc, curveId, points, picked, "Delete Points");
        if (result.ok && points.empty() && state.curve == Object(*r, "target"))
            state.tool = 0;
        return Result(result, sink);
    }
    static EditorStatus Read(void *data, const EditorHost *, const EditorRequest *r, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.DocumentOf(*r);
        const auto target = self.view(r->scope.view);
        if (!target.view) return EDITOR_STALE;
        auto &v = *target.view;
        if (!doc || v.Context() != doc->Context())
            return EDITOR_STALE;
        auto *camera = v.Camera();
        auto *targeted = CKTargetCamera::Cast(camera);
        const auto ref = [&](CK_ID id) { return id && !doc->IsDiscarded(id) ? doc->Reference(id) : ObjectRef{}; };
        EditorField position{"point", EDITOR_REAL_LIST}, coords{"coordinates", EDITOR_REAL_LIST};
        position.reals = {0, 0, 0};
        EditorField objects{"objects", EDITOR_OBJECT_LIST};
        const auto select = Number(*r, "select");
        if (select > 2 || Number(*r, "strict") > 1)
            return EDITOR_INVALID;
        std::vector<CK_ID> ids;
        if (select == 1 && v.Scene()) {
            for (auto cid : {CKCID_3DENTITY, CKCID_2DENTITY}) {
                auto *list = CreateCKObjectArray();
                v.Scene()->ComputeObjectList(list, cid, TRUE);
                for (list->Reset(); !list->EndOfList(); list->Next())
                    if (auto *o = list->GetData(doc->Context()); o && !CKBodyPart::Cast(o) && !o->IsPrivate() &&
                                                                 !v.IsInterfaceObject(o->GetID()) &&
                                                                 !doc->IsDiscarded(o->GetID()))
                        ids.push_back(o->GetID());
                DeleteCKObjectArray(list);
            }
        } else if (select == 2) {
            std::vector<float> rect;
            if (!Vector(Arg(*r, "rectangle"), 4, rect))
                return EDITOR_INVALID;
            ids = v.RectPick(VxRect(rect[0], rect[1], rect[2], rect[3]), Number(*r, "strict") != 0);
        }
        objects.objects = doc->References(ids);
        CK_ID picked = 0, sprite = 0, entity = 0, character = 0;
        int handle = 0;
        VxVector point(0, 0, 0);
        if (const auto *at = Arg(*r, "position"); at && at->reals.count) {
            std::vector<float> xy;
            if (!Vector(at, 2, xy) || Number(*r, "shift") > 1)
                return EDITOR_INVALID;
            const int x = int(xy[0]), y = int(xy[1]);
            auto *curve = CKCurve::Cast(doc->Resolve(Object(*r, "curve")));
            if (Object(*r, "curve") && !curve)
                return EDITOR_STALE;
            LayoutPickInfo info;
            if (curve) {
                const auto pickable = curve->IsPickable();
                struct Restore {
                    CKCurve *curve;
                    CKBOOL value;
                    ~Restore() { curve->SetPickable(value); }
                } restore{curve, pickable};
                curve->SetPickable(FALSE);
                info = v.PickInfo(x, y, false, &point);
                picked = info.character ? info.character : info.entity3d ? info.entity3d : info.entity2d;
                bool found = false;
                for (int i = 0; i < curve->GetControlPointCount(); ++i)
                    if (auto *p = curve->GetControlPoint(i); p && p->GetID() == picked)
                        found = true;
                if (!found)
                    picked = 0;
            } else {
                info = v.PickInfo(x, y, Number(*r, "shift") != 0, &point);
                picked = info.character ? info.character : info.entity3d ? info.entity3d : info.entity2d;
            }
            sprite = info.entity2d;
            entity = info.entity3d;
            character = info.character;
            if (self.selection.Contains(sprite))
                handle = Handle2DAt(CK2dEntity::Cast(doc->Context()->GetObject(sprite)), float(x), float(y));
            position.reals = {point.x, point.y, point.z};
        }
        const auto *g = self.active;
        const bool active = g && g->viewport == target.id && self.Current(*g);
        if (active && g->kind == 2) {
            if (auto *object = CK2dEntity::Cast(doc->Context()->GetObject(g->sprite.Grabbed()))) {
                Vx2DVector p;
                object->GetPosition(p);
                coords.reals = {p.x, p.y};
            }
        } else if (active && g->kind == 1) {
            if (auto *object = CK3dEntity::Cast(doc->Context()->GetObject(g->transform.Grabbed()))) {
                VxVector p;
                if (g->transform.Action() == TransformAction::Rotate) {
                    Vx3DMatrixToEulerAngles(object->GetWorldMatrix(), &p.x, &p.y, &p.z);
                    p *= 180.f / PI;
                } else if (g->transform.Action() == TransformAction::Scale)
                    object->GetScale(&p, FALSE);
                else
                    object->GetPosition(&p);
                coords.reals = {p.x, p.y, p.z};
            }
        }
        auto *object = doc->Context()->GetObject(picked);
        return Write(sink,
                     {{"target", EDITOR_OBJECT, camera ? ref(camera->GetID()) : ObjectRef{}},
                      {"targeted", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(targeted && targeted->GetTarget())},
                      {"perspective",
                       EDITOR_INTEGER_VALUE,
                       {},
                       {},
                       uint64_t(camera && camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION)},
                      {"kind", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(active ? g->kind : 0)},
                      std::move(objects),
                      {"allowed-axes", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(AllowedAxes(v))},
                      {"picked", EDITOR_OBJECT, ref(picked)},
                      {"sprite", EDITOR_OBJECT, ref(sprite)},
                      {"entity", EDITOR_OBJECT, ref(entity)},
                      {"character", EDITOR_OBJECT, ref(character)},
                      {"is-2d", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(CK2dEntity::Cast(object) != nullptr)},
                      {"is-3d", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(CK3dEntity::Cast(object) != nullptr)},
                      {"name", EDITOR_TEXT_VALUE, {}, AnsiToUtf8(object && object->GetName() ? object->GetName() : "")},
                      {"handle", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(handle)},
                      std::move(position),
                      std::move(coords),
                      {"active", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(active)},
                      {"around-selection",
                       EDITOR_INTEGER_VALUE,
                       {},
                       {},
                       uint64_t(active && g->kind == 0 && g->cameraMove.AroundSelection())}});
    }
};
LayoutGestures::LayoutGestures(std::function<Document *()> current, LayoutTargetLookup view, ObjectSelection &selection)
    : m_Data(std::make_unique<Data>(std::move(current), std::move(view), selection)) {}
LayoutGestures::~LayoutGestures() = default;
void LayoutGestures::Poll(EditorApplication &editor) {
    for (auto it = m_Data->tools.begin(); it != m_Data->tools.end();) {
        if (it->first && !editor.Scope(it->first).view)
            it = m_Data->tools.erase(it);
        else
            ++it;
    }
}
bool LayoutGestures::Register(EditorApplication &editor, std::string &error) {
    const EditorParameter queryArgs[] = {
        {EDITOR_TEXT("position"), EDITOR_REAL_LIST, 1},  {EDITOR_TEXT("shift"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("curve"), EDITOR_OBJECT, 1, 1},     {EDITOR_TEXT("select"), EDITOR_INTEGER_VALUE, 1},
        {EDITOR_TEXT("rectangle"), EDITOR_REAL_LIST, 1}, {EDITOR_TEXT("strict"), EDITOR_INTEGER_VALUE, 1}};
    const EditorParameter fields[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT},
                                      {EDITOR_TEXT("targeted"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("allowed-axes"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("picked"), EDITOR_OBJECT},
                                      {EDITOR_TEXT("sprite"), EDITOR_OBJECT},
                                      {EDITOR_TEXT("entity"), EDITOR_OBJECT},
                                      {EDITOR_TEXT("character"), EDITOR_OBJECT},
                                      {EDITOR_TEXT("is-2d"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("is-3d"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("name"), EDITOR_TEXT_VALUE},
                                      {EDITOR_TEXT("handle"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("point"), EDITOR_REAL_LIST},
                                      {EDITOR_TEXT("coordinates"), EDITOR_REAL_LIST},
                                      {EDITOR_TEXT("active"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("around-selection"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("perspective"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},
                                      {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST}};
    const EditorQueryDesc query{sizeof(query),
                                EDITOR_TEXT("layout.input"),
                                EDITOR_TEXT("builtin.layout-gestures"),
                                queryArgs,
                                6,
                                Data::Read,
                                m_Data.get(),
                                EDITOR_QUERY_EACH_READ,
                                EDITOR_DOCUMENT,
                                fields,
                                18};
    const EditorParameter args[] = {
        {EDITOR_TEXT("target"), EDITOR_OBJECT},        {EDITOR_TEXT("kind"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("action"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("objects"), EDITOR_OBJECT_LIST},
        {EDITOR_TEXT("grabbed"), EDITOR_OBJECT, 1, 1}, {EDITOR_TEXT("referent"), EDITOR_OBJECT, 1, 1},
        {EDITOR_TEXT("curve"), EDITOR_OBJECT, 1, 1},   {EDITOR_TEXT("start"), EDITOR_REAL_LIST},
        {EDITOR_TEXT("point"), EDITOR_REAL_LIST, 1},   {EDITOR_TEXT("options"), EDITOR_RECORD},
        {EDITOR_TEXT("value"), EDITOR_RECORD},         {EDITOR_TEXT("started"), EDITOR_REAL_VALUE, 1}};
    const EditorParameter wheelArgs[] = {
        args[0], args[9], {EDITOR_TEXT("notches"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("extent"), EDITOR_REAL_VALUE}};
    const EditorParameter zoomArgs[] = {args[0], args[9], args[3], {EDITOR_TEXT("scene"), EDITOR_INTEGER_VALUE}};
    const EditorParameter clickArgs[] = {{EDITOR_TEXT("sprite"), EDITOR_OBJECT, 0, 1},
                                         {EDITOR_TEXT("entity"), EDITOR_OBJECT, 0, 1},
                                         {EDITOR_TEXT("character"), EDITOR_OBJECT, 0, 1},
                                         {EDITOR_TEXT("double-click"), EDITOR_INTEGER_VALUE}};
    const EditorParameter curveArgs[] = {args[0],
                                         {EDITOR_TEXT("action"), EDITOR_INTEGER_VALUE},
                                         {EDITOR_TEXT("position"), EDITOR_REAL_LIST},
                                         {EDITOR_TEXT("right"), EDITOR_INTEGER_VALUE},
                                         {EDITOR_TEXT("plane"), EDITOR_INTEGER_VALUE},
                                         {EDITOR_TEXT("plane-distance"), EDITOR_REAL_VALUE}};
    const EditorParameter toolArgs[] = {{EDITOR_TEXT("target"), EDITOR_OBJECT, 0, 1},
                                        {EDITOR_TEXT("tool"), EDITOR_INTEGER_VALUE},
                                        {EDITOR_TEXT("plane"), EDITOR_INTEGER_VALUE}};
    const EditorParameter toolFields[] = {toolArgs[0], toolArgs[1], toolArgs[2]};
    const EditorQueryDesc queries[] = {query,
                                       {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.curve-tool"),
                                        EDITOR_TEXT("builtin.layout-gestures"), nullptr, 0, Data::Tool, m_Data.get(),
                                        EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, toolFields, 3}};
    EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.edit-gesture"), EDITOR_TEXT("builtin.layout-gestures"),
         EDITOR_TEXT("Edit Layout"), args, 12, Data::Run, m_Data.get(), EDITOR_DOCUMENT, EDITOR_DOCUMENT_EDIT,
         Data::Check, Data::Preview, Data::Commit},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.wheel-zoom"), EDITOR_TEXT("builtin.layout-gestures"),
         EDITOR_TEXT("Zoom Camera"), wheelArgs, 4, Data::Act, &m_Data->entries[0], EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.zoom-on"), EDITOR_TEXT("builtin.layout-gestures"),
         EDITOR_TEXT("Frame Objects"), zoomArgs, 4, Data::Act, &m_Data->entries[1], EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_CHANGE},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.player-click"), EDITOR_TEXT("builtin.layout-gestures"),
         EDITOR_TEXT("Click in Player"), clickArgs, 4, Data::Act, &m_Data->entries[2], EDITOR_DOCUMENT,
         EDITOR_RUNTIME_CONTROL},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.curve-click"), EDITOR_TEXT("builtin.layout-gestures"),
         EDITOR_TEXT("Edit Curve Points"), curveArgs, 6, Data::Act, &m_Data->entries[3], EDITOR_DOCUMENT,
         EDITOR_DOCUMENT_EDIT},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.set-curve-tool"), EDITOR_TEXT("builtin.layout-gestures"),
         EDITOR_TEXT("Select Curve Tool"), toolArgs, 3, Data::Act, &m_Data->entries[4], EDITOR_DOCUMENT,
         EDITOR_SESSION_CHANGE}};
    for (auto &command : commands) command.view = EDITOR_TEXT("layout.viewport");
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("layout.gestures");
    module.queries = queries;
    module.queryCount = 2;
    module.commands = commands;
    module.commandCount = 6;
    return editor.Register(module, error);
}
} // namespace vdev
