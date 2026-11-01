#include "features/layout/LayoutPresentation.h"
#include "features/layout/LayoutViewport.h"
#include "application/ObjectSelection.h"
#include "application/Preferences.h"
#include "core/document/Document.h"
#include "core/layout/LayoutView.h"
#include "core/layout/LayoutExplorer.h"
#include "core/layout/LayoutTools.h"
#include "core/runtime/TextEncoding.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <cmath>
#include <string_view>

namespace vdev {
namespace {
float Component(const std::string &text, int index, char separator) {
    size_t start = 0;
    for (int i = 0; i < index; ++i) {
        const size_t next = text.find(separator, start);
        if (next == std::string::npos)
            return 0.0f;
        start = next + 1;
    }
    return static_cast<float>(std::atof(text.c_str() + start));
}

VxVector Vector(const std::string &text) {
    return VxVector(Component(text, 0, '|'), Component(text, 1, '|'), Component(text, 2, '|'));
}

bool SameDisplay(const LayoutDisplay &a, const LayoutDisplay &b) {
    return a.shading == b.shading && a.textured == b.textured && a.wireframe == b.wireframe &&
           a.showFrames == b.showFrames && a.showCurves == b.showCurves && a.showWorldAxis == b.showWorldAxis &&
           a.showLights == b.showLights && a.showCameras == b.showCameras && a.showObjectAxis == b.showObjectAxis &&
           a.showPortals == b.showPortals && a.showGrid == b.showGrid && a.ignoreCameraAspect == b.ignoreCameraAspect &&
           a.gridOrientation == b.gridOrientation && a.gridSections[0] == b.gridSections[0] &&
           a.gridSections[1] == b.gridSections[1] && a.gridStep[0] == b.gridStep[0] && a.gridStep[1] == b.gridStep[1] &&
           a.gridColor == b.gridColor && a.gridDir == b.gridDir && a.gridUp == b.gridUp && a.gridRight == b.gridRight &&
           a.constantObjectAxis == b.constantObjectAxis && a.objectAxisZoom == b.objectAxisZoom &&
           a.constantFrame == b.constantFrame && a.constantCamera == b.constantCamera &&
           a.constantLight == b.constantLight && a.constantCurvePoint == b.constantCurvePoint &&
           a.frameZoom == b.frameZoom && a.cameraZoom == b.cameraZoom && a.lightZoom == b.lightZoom &&
           a.curvePointZoom == b.curvePointZoom && a.showScreenGrid == b.showScreenGrid &&
           a.homogenousScreenGrid == b.homogenousScreenGrid && a.screenStep[0] == b.screenStep[0] &&
           a.screenStep[1] == b.screenStep[1] && a.screenOffset[0] == b.screenOffset[0] &&
           a.screenOffset[1] == b.screenOffset[1];
}

EditorStatus Write(EditorSink sink, const std::vector<EditorField> &fields) {
    const auto values = BorrowEditorFields(fields);
    return sink.write ? sink.write(sink.data, values.data(), uint32_t(values.size())) : EDITOR_INVALID;
}
const EditorValue *Arg(const EditorRequest &request, const char *key) {
    for (uint32_t i = 0; i < request.count; ++i)
        if (std::string_view(request.values[i].key.data, request.values[i].key.size) == key)
            return request.values + i;
    return nullptr;
}
} // namespace
struct LayoutPresentation::Data {
    EditorApplication &editor;
    std::function<Document *()> current;
    std::function<LayoutView &()> view;
    LayoutViewport &viewport;
    ObjectSelection &selection;
    const Preferences &preferences;
    std::function<bool()> running;
    DocumentToken document{};
    ObjectRef level{};
    std::vector<ObjectRef> playerSelection;
    bool player = false;
    uint64_t composition = 0;
    enum Action { Toggle, Leave };
    struct Entry {
        Data *owner;
        Action action;
    } entries[2]{{this, Toggle}, {this, Leave}};
    bool Player() const {
        auto *doc = current();
        return doc && doc->IsOpen() && doc->Context()->IsPlaying() && running();
    }
    EditorStatus FullScreen(bool leave, bool activate, EditorSink sink) {
        std::string error;
        bool ok = true;
        if (leave || view().FullScreen()) {
            if (!view().FullScreen())
                return EDITOR_NO_CHANGE;
            ok = viewport.ExitFullScreen(activate, error);
        } else {
            if (!view().Started() || !Player())
                return EDITOR_REFUSED;
            auto *doc = current();
            const int driver =
                LayoutFullScreenDriver(doc->Context(), preferences.Text("3D Layout\\Player Mode\\FullScren Driver"));
            VxDisplayMode mode;
            const auto wanted = preferences.Text("3D Layout\\Player Mode\\Render Mode");
            if (!LayoutFullScreenMode(doc->Context(), driver, wanted, mode)) {
                Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, "3D Layout: unavailable fullscreen mode " + wanted}});
                return EDITOR_REFUSED;
            }
            ok = viewport.EnterFullScreen({driver, mode.Width, mode.Height, mode.Bpp, mode.RefreshRate}, error);
        }
        if (!ok)
            Write(sink, {{"message", EDITOR_TEXT_VALUE, {}, error}});
        return ok ? EDITOR_OK : EDITOR_ERROR;
    }
    void Automatic(const char *command) {
        const auto scope = editor.Scope();
        const auto result = editor.Execute(command, {scope, nullptr, 0});
        if (!result.message.empty() && editor.report)
            editor.report(result);
    }
    void Sync() {
        auto *doc = current();
        const auto token = doc && doc->IsOpen() ? doc->Token() : DocumentToken{};
        const bool replaced = token != document;
        if (replaced) {
            document = token;
            level = {};
            playerSelection.clear();
            player = false;
        }
        const bool nextPlayer = Player();
        const bool changed = nextPlayer != player;
        player = nextPlayer;
        if (changed && player) {
            playerSelection = preferences.Bool("3D Layout\\Player Mode\\Auto Show Selection")
                                  ? doc->References(selection.Objects())
                                  : std::vector<ObjectRef>();
        }
        if ((changed || replaced) && !player && view().FullScreen())
            Automatic("layout.leave-fullscreen");
        if (view().Started()) {
            auto *nativeLevel = token.value ? doc->Context()->GetCurrentLevel() : nullptr;
            const auto nextLevel = nativeLevel ? doc->Reference(nativeLevel->GetID()) : ObjectRef{};
            if (replaced || nextLevel != level || !view().InterfaceAlive()) {
                view().CompositionChanged();
                level = nextLevel;
                ++composition;
            }
        }
        std::vector<CK_ID> shown;
        if (player) {
            shown.assign(doc->Layout().playerShown.begin(), doc->Layout().playerShown.end());
            for (auto ref : playerSelection)
                if (doc->Resolve(ref))
                    shown.push_back(ref.object);
        }
        view().SetPlayerShown(shown);
        const auto &p = preferences;
        const std::string mode = player ? "3D Layout\\Player Mode\\" : "3D Layout\\Author Mode\\";
        LayoutDisplay display;
        display.shading = static_cast<VxShadeType>(std::clamp(p.Int(mode + "Render Type Index"), 0, 3));
        display.textured = p.Bool(mode + "Render Type Textured");
        display.wireframe = p.Bool(mode + "Render Type Wireframe");
        display.showFrames = p.Bool(mode + "Show Frames");
        display.showCurves = p.Bool(mode + "Show Curves");
        display.showLights = p.Bool(mode + "Show Lights");
        display.showCameras = p.Bool(mode + "Show Cameras");
        display.showPortals = p.Bool(mode + "Show Portals");
        // The objects' axes stay (Dev.exe sub_4A1E00 +484); the world axis and
        // the Screen Guide are Author Mode's; the reference guide shows in Player
        // Mode when Show Reference Guides is on too (sub_4A11D0 +524).
        display.showObjectAxis = p.Bool("3D Layout\\Author Mode\\Show ObjectAxis");
        display.showWorldAxis = !player && p.Bool("3D Layout\\Author Mode\\Show WorldAxis");
        display.showGrid =
            p.Bool("3D Layout\\Author Mode\\Show Grid") && (!player || p.Bool("3D Layout\\Player Mode\\Show Grid"));
        display.ignoreCameraAspect = p.Bool("3D Layout\\Environment\\Ignore Camera Aspect Ratio");
        display.constantObjectAxis = p.Int("3D Layout\\Interface Objects\\Constant Object Axis");
        display.objectAxisZoom = p.Float("3D Layout\\Interface Objects\\Object Axis Zoom Percentage");
        display.constantFrame = p.Bool("3D Layout\\Interface Objects\\Constant Frame");
        display.constantCamera = p.Bool("3D Layout\\Interface Objects\\Constant Camera");
        display.constantLight = p.Bool("3D Layout\\Interface Objects\\Constant Light");
        display.constantCurvePoint = p.Bool("3D Layout\\Interface Objects\\Constant Curve Point");
        display.frameZoom = p.Float("3D Layout\\Interface Objects\\Frame Zoom Percentage");
        display.cameraZoom = p.Float("3D Layout\\Interface Objects\\Camera Zoom Percentage");
        display.lightZoom = p.Float("3D Layout\\Interface Objects\\Light Zoom Percentage");
        display.curvePointZoom = p.Float("3D Layout\\Interface Objects\\Curve Point Zoom Percentage");
        display.gridOrientation = p.Int("3D Layout\\Reference Grid\\Orientation");
        const std::string sections = p.Text("3D Layout\\Reference Grid\\Sections"),
                          step = p.Text("3D Layout\\Reference Grid\\Step");
        display.gridSections[0] = std::max(1, static_cast<int>(Component(sections, 0, ';')));
        display.gridSections[1] = std::max(1, static_cast<int>(Component(sections, 1, ';')));
        display.gridStep[0] = Component(step, 0, ';');
        display.gridStep[1] = Component(step, 1, ';');
        display.gridColor = static_cast<CKDWORD>(p.Int("3D Layout\\Reference Grid\\Color"));
        display.gridDir = Vector(p.Text("3D Layout\\Reference Grid\\Orientation Dir"));
        display.gridUp = Vector(p.Text("3D Layout\\Reference Grid\\Orientation Up"));
        display.gridRight = Vector(p.Text("3D Layout\\Reference Grid\\Orientation Right"));
        display.showScreenGrid = !player && p.Bool("3D Layout\\Show Screen Grid");
        display.homogenousScreenGrid = p.Bool("3D Layout\\Homogenous Screen Grid");
        const std::string screenStep = p.Text("3D Layout\\Screen Step"),
                          screenOffset = p.Text("3D Layout\\Screen Offset");
        display.screenStep[0] = Component(screenStep, 0, '|');
        display.screenStep[1] = Component(screenStep, 1, '|');
        display.screenOffset[0] = Component(screenOffset, 0, '|');
        display.screenOffset[1] = Component(screenOffset, 1, '|');
        if (!SameDisplay(display, view().Display()))
            view().SetDisplay(display);

        if (doc && token.value) {
            view().SetSelection(selection.Objects());
            view().SetCones(std::vector<CK_ID>(doc->Layout().cones.begin(), doc->Layout().cones.end()));
            view().SetRepresentations(doc->Layout().representations);
            view().SetHiddenCurvePoints(doc->Layout().hiddenCurvePoints);
        } else {
            view().SetSelection({});
            view().SetCones({});
            view().SetRepresentations({});
            view().SetHiddenCurvePoints({});
        }
        if (changed && player && p.Bool("3D Layout\\Player Mode\\Auto FullScreen")) {
            Automatic("layout.toggle-fullscreen");
        }
    }
    std::string DebugText(int x, int y) {
        auto *doc = current();
        if (!doc || !doc->IsOpen())
            return {};
        auto *context = doc->Context();
        char line[512];
        std::string text;
        const bool inside = x >= 0 && y >= 0 && x < view().Width() && y < view().Height();
        const auto info = inside ? view().PickInfo(x, y) : LayoutPickInfo();
        // 32 characters (cut as wide characters, not ANSI bytes: a double-byte one stays whole), UTF-8.
        auto name = [](CKObject *object) {
            return WideToUtf8(AnsiToWide(object->GetName() ? object->GetName() : "Unknown").substr(0, 32));
        };
        auto entry = [&](CK_ID id, bool marks) {
            CKObject *object = context->GetObject(id);
            if (!object)
                return;
            text += DevClassName(object) + ": " + name(object);
            if (marks && selection.Contains(id))
                text += " (S)";
            if (marks && view().DropTarget() == id)
                text += " (D)";
            text += '\n';
        };
        entry(info.entity2d, true);
        entry(info.character, true);
        entry(info.entity3d, true);
        CKMesh *mesh = CKMesh::Cast(context->GetObject(info.mesh));
        if (mesh) {
            text += DevClassName(mesh) + ": " + name(mesh) + "\n";
            if (info.face >= 0 && info.face < mesh->GetFaceCount()) {
                VxVector normal = mesh->GetFaceNormal(info.face);
                normal.Normalize();
                int index[3] = {0, 0, 0};
                mesh->GetFaceVertexIndex(info.face, index[0], index[1], index[2]);
                int nearest = index[0];
                float best = -1.0f;
                for (int k = 0; k < 3; ++k) {
                    VxVector position;
                    mesh->GetVertexPosition(index[k], &position);
                    const float distance = SquareMagnitude(position - info.point);
                    if (best < 0.0f || distance < best)
                        best = distance, nearest = index[k];
                }
                std::snprintf(line, sizeof(line), "U=%lf V=%lf Base Material\n", info.u, info.v);
                text += line;
                for (int channel = 0; channel < mesh->GetChannelCount(); ++channel) {
                    float u = 0.0f, v = 0.0f;
                    mesh->GetVertexTextureCoordinates(nearest, &u, &v, channel);
                    std::snprintf(line, sizeof(line), "U=%lf V=%lf channel=%d\n", u, v, channel);
                    text += line;
                }
                std::snprintf(line, sizeof(line), "Face: %7d  Normal: %.4f %.4f %.4f \nNearest Vertex: %6d\n",
                              info.face, normal.x, normal.y, normal.z, nearest);
                text += line;
                if (mesh->GetLitMode() == VX_PRELITMESH) {
                    const CKDWORD diffuse = mesh->GetVertexColor(nearest),
                                  specular = mesh->GetVertexSpecularColor(nearest);
                    std::snprintf(line, sizeof(line), "Vertex Diffuse  R:%4d G:%4d B:%4d A:%4d\n",
                                  (diffuse >> 16) & 255, (diffuse >> 8) & 255, diffuse & 255, diffuse >> 24);
                    text += line;
                    std::snprintf(line, sizeof(line), "Vertex Specular R:%4d G:%4d B:%4d A:%4d\n",
                                  (specular >> 16) & 255, (specular >> 8) & 255, specular & 255, specular >> 24);
                    text += line;
                }
            }
        }
        if (CKObject *material = context->GetObject(info.material))
            text += DevClassName(material) + ": " + AnsiToUtf8(name(material)) + "\n";
        if (CKObject *texture = context->GetObject(info.texture)) {
            text += DevClassName(texture) + ": " + AnsiToUtf8(name(texture));
            if (mesh) {
                std::snprintf(line, sizeof(line), "  U:%.2f V:%.2f\n", info.u, info.v);
                text += line;
            } else {
                text += '\n';
            }
        }

        return text;
    }
    static EditorStatus Pick(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto *doc = self.current();
        if (!doc || !doc->IsOpen() || doc->Token() != request->scope.document)
            return EDITOR_STALE;
        const auto *x = Arg(*request, "x"), *y = Arg(*request, "y"), *cid = Arg(*request, "class");
        if (!x || !y || !cid || !std::isfinite(x->real) || !std::isfinite(y->real) || x->real < 0 || y->real < 0 ||
            x->real >= self.view().Width() || y->real >= self.view().Height() || cid->integer > INT_MAX)
            return EDITOR_INVALID;
        auto *camera = self.view().Camera();
        const auto shown = camera && !self.view().IsViewCamera(camera->GetID()) ? camera->GetID() : 0;
        const auto id = LayoutPickObject(doc->Context(), self.view().PickInfo(int(x->real), int(y->real)),
                                         int(cid->integer), shown);
        return Write(sink, {{"object", EDITOR_OBJECT, doc->IsDiscarded(id) ? ObjectRef{} : doc->Reference(id)}});
    }
    static EditorStatus Shortcuts(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        std::vector<EditorField> fields;
        for (const auto *action : {"runtime.play-pause", "runtime.step-frame", "runtime.reset", "runtime.pause"}) {
            EditorField field{action, EDITOR_TEXT_LIST};
            for (const auto &binding : self.editor.Bindings())
                if (binding.first == action)
                    field.texts.push_back(binding.second);
            fields.push_back(std::move(field));
        }
        return Write(sink, fields);
    }
    static EditorStatus State(void *data, const EditorHost *, const EditorRequest *, EditorSink sink) {
        auto &self = *static_cast<Data *>(data);
        auto &view = self.view();
        auto *doc = self.current();
        auto *camera = view.Camera();
        std::string selected = "Nothing selected";
        const auto &objects = self.selection.Objects();
        if (objects.size() > 1)
            selected = std::to_string(objects.size()) + " objects selected";
        else if (!objects.empty() && doc && doc->IsOpen()) {
            auto *object = doc->Context()->GetObject(objects.front());
            selected = "'" + AnsiToUtf8(object && object->GetName() ? object->GetName() : "") + "' selected";
        }
        return Write(
            sink,
            {{"ready", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(view.Started())},
             {"player", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(self.Player())},
             {"playing", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(doc && doc->IsOpen() && doc->Context()->IsPlaying())},
             {"fullscreen", EDITOR_INTEGER_VALUE, {}, {}, uint64_t(view.FullScreen())},
             {"composition", EDITOR_INTEGER_VALUE, {}, {}, self.composition},
             {"perspective",
              EDITOR_INTEGER_VALUE,
              {},
              {},
              uint64_t(camera && camera->GetProjectionType() == CK_PERSPECTIVEPROJECTION)},
             {"targeted",
              EDITOR_INTEGER_VALUE,
              {},
              {},
              uint64_t(CKTargetCamera::Cast(camera) && CKTargetCamera::Cast(camera)->GetTarget())},
             {"selection-text", EDITOR_TEXT_VALUE, {}, std::move(selected)},
             {"frame-shapes", EDITOR_TEXT_LIST, {}, {}, 0, {}, {}, view.FrameShapeNames()}});
    }
    static EditorStatus Debug(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        const auto *x = Arg(*request, "x"), *y = Arg(*request, "y");
        if (!x || !y || !std::isfinite(x->real) || !std::isfinite(y->real) || x->real < INT_MIN || x->real > INT_MAX ||
            y->real < INT_MIN || y->real > INT_MAX)
            return EDITOR_INVALID;
        return Write(
            sink, {{"text", EDITOR_TEXT_VALUE, {}, static_cast<Data *>(data)->DebugText(int(x->real), int(y->real))}});
    }
    static EditorStatus Run(void *data, const EditorHost *, const EditorRequest *request, EditorSink sink) {
        auto &entry = *static_cast<Entry *>(data);
        const auto *deactivated = Arg(*request, "deactivated");
        if (deactivated && deactivated->integer > 1)
            return EDITOR_INVALID;
        return entry.owner->FullScreen(entry.action == Leave, !deactivated || !deactivated->integer, sink);
    }
};
LayoutPresentation::LayoutPresentation(EditorApplication &editor, std::function<Document *()> current,
                                       std::function<LayoutView &()> view, LayoutViewport &viewport,
                                       ObjectSelection &selection, const Preferences &preferences,
                                       std::function<bool()> running)
    : m_Data(new Data{editor, std::move(current), std::move(view), viewport, selection, preferences,
                      std::move(running)}) {}
LayoutPresentation::~LayoutPresentation() = default;
void LayoutPresentation::Poll() {
    if (m_Data->editor.NativeAvailable())
        m_Data->Sync();
}
const std::vector<ProviderChoice> &LayoutPresentation::DefaultProviders() {
    static const std::vector<ProviderChoice> result{
        {"query", "layout.presentation", "builtin.layout-presentation"},
        {"query", "layout.debug-data", "builtin.layout-presentation"},
        {"query", "layout.pick-object", "builtin.layout-presentation"},
        {"query", "layout.player-shortcuts", "builtin.layout-presentation"},
        {"command", "layout.toggle-fullscreen", "builtin.layout-presentation"},
        {"command", "layout.leave-fullscreen", "builtin.layout-presentation"}};
    return result;
}
bool LayoutPresentation::Register(std::string &error) {
    const EditorParameter stateFields[] = {
        {EDITOR_TEXT("ready"), EDITOR_INTEGER_VALUE},       {EDITOR_TEXT("player"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("playing"), EDITOR_INTEGER_VALUE},     {EDITOR_TEXT("fullscreen"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("composition"), EDITOR_INTEGER_VALUE}, {EDITOR_TEXT("perspective"), EDITOR_INTEGER_VALUE},
        {EDITOR_TEXT("targeted"), EDITOR_INTEGER_VALUE},    {EDITOR_TEXT("selection-text"), EDITOR_TEXT_VALUE},
        {EDITOR_TEXT("frame-shapes"), EDITOR_TEXT_LIST}};
    const EditorParameter point[] = {{EDITOR_TEXT("x"), EDITOR_REAL_VALUE}, {EDITOR_TEXT("y"), EDITOR_REAL_VALUE}};
    const EditorParameter text{EDITOR_TEXT("text"), EDITOR_TEXT_VALUE};
    const EditorParameter shortcutFields[] = {{EDITOR_TEXT("runtime.play-pause"), EDITOR_TEXT_LIST},
                                              {EDITOR_TEXT("runtime.step-frame"), EDITOR_TEXT_LIST},
                                              {EDITOR_TEXT("runtime.reset"), EDITOR_TEXT_LIST},
                                              {EDITOR_TEXT("runtime.pause"), EDITOR_TEXT_LIST}};
    const EditorParameter pickArgs[] = {point[0], point[1], {EDITOR_TEXT("class"), EDITOR_INTEGER_VALUE}};
    const EditorParameter object{EDITOR_TEXT("object"), EDITOR_OBJECT};
    const EditorQueryDesc queries[] = {
        {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.pick-object"), EDITOR_TEXT("builtin.layout-presentation"),
         pickArgs, 3, Data::Pick, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &object, 1},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.player-shortcuts"), EDITOR_TEXT("builtin.layout-presentation"),
         nullptr, 0, Data::Shortcuts, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_APPLICATION, shortcutFields, 4},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.presentation"), EDITOR_TEXT("builtin.layout-presentation"),
         nullptr, 0, Data::State, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, stateFields,
         uint32_t(std::size(stateFields))},
        {sizeof(EditorQueryDesc), EDITOR_TEXT("layout.debug-data"), EDITOR_TEXT("builtin.layout-presentation"), point,
         2, Data::Debug, m_Data.get(), EDITOR_QUERY_EACH_READ, EDITOR_DOCUMENT, &text, 1}};
    const EditorParameter deactivated{EDITOR_TEXT("deactivated"), EDITOR_INTEGER_VALUE, 1};
    const EditorCommandDesc commands[] = {
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.toggle-fullscreen"), EDITOR_TEXT("builtin.layout-presentation"),
         EDITOR_TEXT("Toggle Fullscreen"), nullptr, 0, Data::Run, &m_Data->entries[0], EDITOR_DOCUMENT,
         EDITOR_RUNTIME_CONTROL},
        {sizeof(EditorCommandDesc), EDITOR_TEXT("layout.leave-fullscreen"), EDITOR_TEXT("builtin.layout-presentation"),
         EDITOR_TEXT("Leave Fullscreen"), &deactivated, 1, Data::Run, &m_Data->entries[1], EDITOR_APPLICATION,
         EDITOR_RUNTIME_CONTROL}};
    EditorModuleDesc module{};
    module.size = sizeof(module);
    module.version = EDITOR_VERSION;
    module.id = EDITOR_TEXT("layout.presentation");
    module.commands = commands;
    module.commandCount = 2;
    module.queries = queries;
    module.queryCount = 4;
    return m_Data->editor.Register(module, error);
}
} // namespace vdev
