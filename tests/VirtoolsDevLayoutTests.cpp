// The 3D Layout view (--layout).
#include "VirtoolsDevTestSupport.h"
#include "VirtoolsRuntimeCheck.h"

namespace vdevtest {

int LayoutFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    namespace fs = std::filesystem;
    std::cout << std::unitbuf; // lines reach the log before a runtime crash
    fs::create_directories(outputDirectory);
    CKContext *context = host.Context();
    int failures = 0;
    auto check = [&](bool condition, const std::string &what) {
        if (!condition) {
            std::cerr << "layout: " << what << '\n';
            ++failures;
        }
    };
    std::string error;
    Document document(host);
    UndoHistory &undo = document.History();
    undo.SetLimit(1000); // deep enough to count the steps
    check(document.New(&error), "new: " + error);
    LayoutView view(host);
    if (!view.Start("", &error)) {
        // No render device: the rendered checks are skipped (the others are LayoutObjectsFile's).
        std::cerr << "skipped: layout: " << error << '\n';
        return SKIPPED;
    }
    CKRenderContext *rc = view.RenderContext();
    {
        std::string icons;
        for (const std::string &name : view.IconNames()) icons += " " + name;
        std::cout << "layout: interface icons" << icons << std::endl;
    }
    std::cout << "layout: driver " << rc->GetDriverIndex() << " of "
              << context->GetRenderManager()->GetRenderDriverCount() << '\n';
    CKLevel *level = context->GetCurrentLevel();
    check(level && level->GetRenderContextCount() == 1 && level->GetRenderContext(0) == rc,
          "the render context is the level's");
    check(view.Camera() && view.Camera()->GetID() == view.ViewCamera(LayoutViewCamera::Perspective),
          "the view starts on the Perspective View");
    check(view.Cameras().size() == LAYOUT_VIEW_CAMERA_COUNT, "Dev's five views, no composition camera");
    {
        // Dev's Perspective View looks down at its target at the origin.
        VxVector dir, up;
        view.Camera()->GetOrientation(&dir, &up);
        check(dir.y < -0.1f && dir.z > 0.9f, "the Perspective View aims at its target");
    }
    std::vector<unsigned char> pixels;
    auto pixel = [&](int x, int y) {
        const unsigned char *p = &pixels[(static_cast<size_t>(y) * view.Width() + x) * 4];
        return (static_cast<CKDWORD>(p[2]) << 16) | (static_cast<CKDWORD>(p[1]) << 8) | p[0];
    };
    auto count = [&](int x0, int y0, int x1, int y1, const std::function<bool(CKDWORD)> &test) {
        int found = 0;
        for (int y = std::max(0, y0); y < std::min(view.Height(), y1); ++y)
            for (int x = std::max(0, x0); x < std::min(view.Width(), x1); ++x)
                if (test(pixel(x, y))) ++found;
        return found;
    };
    auto white = [](CKDWORD c) { return (c & 0xFF) > 230 && ((c >> 8) & 0xFF) > 230 && ((c >> 16) & 0xFF) > 230; };
    auto red = [](CKDWORD c) { return ((c >> 16) & 0xFF) > 180 && ((c >> 8) & 0xFF) < 80 && (c & 0xFF) < 80; };
    // ViewProjection against the render context's own transform, on the frame
    // just drawn: the same pixels and depths.
    auto projectionAgrees = [&](const char *camera) {
        VxRect rect;
        rc->GetViewRect(rect);
        const ViewProjection projection = ViewProjection::Of(rc->GetAttachedCamera(), rect);
        float worst = 0.0f, worstDepth = 0.0f;
        for (float fx : {0.1f, 0.5f, 0.9f})
            for (float fy : {0.2f, 0.5f, 0.8f})
                for (float along : {1.5f, 10.0f, 200.0f}) {
                    VxVector origin, direction;
                    if (!projection.Ray(rect.left + fx * rect.GetWidth(), rect.top + fy * rect.GetHeight(), origin, direction))
                        continue;
                    const VxVector point = origin + direction * along;
                    VxVector expected;
                    ScreenOfWorldPoint(rc, &expected, point);
                    const VxVector got = projection.Project(point);
                    worst = std::max({worst, std::fabs(got.x - expected.x), std::fabs(got.y - expected.y)});
                    worstDepth = std::max(worstDepth, std::fabs(got.z - expected.z));
                }
        std::cout << "layout: " << camera << ": ViewProjection within " << worst << " px, depth " << worstDepth
                  << " of the render context's Transform\n";
        check(worst < 0.05f && worstDepth < 0.001f, std::string(camera) + ": ViewProjection projects as the render context does");
    };
    check(view.Render(640, 480, pixels) && view.Width() == 640 && view.Height() == 480, "a 640 x 480 frame");
    if (failures) return 1;
    WriteFrame((fs::path(outputDirectory) / "new.bmp").string(), view.Width(), view.Height(), pixels);
    projectionAgrees("Perspective View");
    std::cout << "layout: corner " << std::hex << pixel(2, 2) << std::dec << ", grid pixels "
              << count(0, 0, 640, 480, white) << ", axis red pixels " << count(0, 300, 160, 480, red) << '\n';
    check(pixel(2, 2) == 0x808080, "Dev's new-scene background");
    check(count(0, 0, 640, 480, white) > 2000, "the reference grid");
    check(count(0, 300, 160, 480, red) > 20, "the world axis X in the bottom-left corner");
    {
        // The Screen Guide (Dev.exe sub_4AF910): white lines 20 pixels apart,
        // at 20.5, 40.5, ... with no offset.
        LayoutDisplay guide = view.Display();
        guide.showGrid = false;
        guide.showScreenGrid = true;
        view.SetDisplay(guide);
        check(view.Render(640, 480, pixels), "a frame with the Screen Guide");
        int onLines = 0, between = 0;
        for (int y = 11; y < 470; y += 30) {
            onLines += white(pixel(20, y)) && white(pixel(40, y)) ? 1 : 0;
            between += white(pixel(30, y)) ? 1 : 0;
        }
        std::cout << "layout: screen guide " << onLines << " rows on its lines, " << between << " between\n";
        check(onLines >= 12 && between == 0, "the Screen Guide's columns every 20 pixels");
        check(white(pixel(5, 20)) && white(pixel(5, 40)) && !white(pixel(5, 30)), "and its rows");
        guide.showScreenGrid = false;
        guide.showGrid = true;
        view.SetDisplay(guide);
    }
    // A wider frame keeps the camera's 4:3: the view is letterboxed.
    check(view.Render(800, 400, pixels), "an 800 x 400 frame");
    const VxRect wide = view.ViewRect();
    std::cout << "layout: 800 x 400 view rect " << wide.left << "," << wide.top << " " << wide.GetWidth() << " x "
              << wide.GetHeight() << '\n';
    check(std::fabs(wide.GetWidth() / wide.GetHeight() - 4.0f / 3.0f) < 0.02f, "the view keeps the camera's 4:3");
    // Without a render context the view computes the viewport (LayoutViewRect): CK's, within a pixel.
    auto sameRect = [](const VxRect &a, const VxRect &b) {
        return std::fabs(a.left - b.left) <= 1.0f && std::fabs(a.top - b.top) <= 1.0f &&
               std::fabs(a.right - b.right) <= 1.0f && std::fabs(a.bottom - b.bottom) <= 1.0f;
    };
    const VxRect wideFitted = LayoutViewRect(800, 400, view.Camera(), true);
    std::cout << "layout: computed 800 x 400 view rect " << wideFitted.left << "," << wideFitted.top << " "
              << wideFitted.GetWidth() << " x " << wideFitted.GetHeight() << '\n';
    check(sameRect(wide, wideFitted), "the computed viewport of a wide frame is CK's");
    std::vector<unsigned char> tallPixels;
    check(view.Render(400, 600, tallPixels), "a 400 x 600 frame");
    const VxRect tall = view.ViewRect(), tallFitted = LayoutViewRect(400, 600, view.Camera(), true);
    std::cout << "layout: 400 x 600 view rect " << tall.left << "," << tall.top << " " << tall.GetWidth() << " x "
              << tall.GetHeight() << ", computed " << tallFitted.left << "," << tallFitted.top << " "
              << tallFitted.GetWidth() << " x " << tallFitted.GetHeight() << '\n';
    check(sameRect(tall, tallFitted), "the computed viewport of a tall frame is CK's");
    WriteFrame((fs::path(outputDirectory) / "wide.bmp").string(), view.Width(), view.Height(), pixels);
    LayoutDisplay display = view.Display();
    display.showGrid = false;
    view.SetDisplay(display);
    check(view.Render(640, 480, pixels) && count(0, 0, 640, 480, white) < 50, "Show Grid off hides the grid");
    display.showGrid = true;
    view.SetDisplay(display);
    view.SetCamera(view.ViewCamera(LayoutViewCamera::Top));
    check(view.Camera() && view.Camera()->GetID() == view.ViewCamera(LayoutViewCamera::Top), "Top View");
    check(view.Render(640, 480, pixels), "a Top View frame");
    WriteFrame((fs::path(outputDirectory) / "top.bmp").string(), view.Width(), view.Height(), pixels);
    projectionAgrees("Top View");

    // Camera tools (Dev.exe sub_48BA60, sub_4942A0, sub_4936C0, sub_48D750) on
    // the Perspective View, 640 x 480, without constant speed.
    view.SetCamera(view.ViewCamera(LayoutViewCamera::Perspective));
    check(view.Render(640, 480, pixels), "a Perspective View frame");
    CKCamera *perspective = view.Camera();
    CK3dEntity *target = CKTargetCamera::Cast(perspective)->GetTarget();
    auto position = [](CK3dEntity *entity) {
        VxVector p;
        entity->GetPosition(&p);
        return p;
    };
    CameraSteps steps;
    steps.zoomConstant = steps.translateConstant = steps.rotateConstant = steps.fovConstant = false;
    const VxVector start = position(perspective);
    // Steps counted from a mark: the count stops at the undo depth.
    const size_t undoBefore = undo.UndoCount();
    CameraGesture gesture;
    // Zoom: 48 pixels up = 500 * 48 / 480 = 50 toward the target.
    check(gesture.Begin(view, CameraAction::Zoom, steps, 320, 240, {}, false, false, false), "zoom begins");
    gesture.Move(320, 192, false);
    const float zoomed = Magnitude(position(target) - position(perspective));
    std::cout << "layout: zoom from " << Magnitude(position(target) - start) << " to " << zoomed << '\n';
    check(std::fabs(Magnitude(position(target) - start) - zoomed - 50.0f) < 0.01f, "zoom moves 50 toward the target");
    gesture.Cancel();
    check(Magnitude(position(perspective) - start) < 0.001f, "cancel restores the camera");
    // Dolly: the target moves too.
    const VxVector targetStart = position(target);
    check(gesture.Begin(view, CameraAction::Dolly, steps, 320, 240, {}, false, false, false), "dolly begins");
    gesture.Move(320, 192, false);
    check(std::fabs(Magnitude(position(target) - targetStart) - 50.0f) < 0.01f &&
              std::fabs(Magnitude(position(perspective) - start) - 50.0f) < 0.01f,
          "dolly moves the camera and its target 50");
    check(gesture.End(&document) && (undo.UndoCount() - undoBefore) == 1, "the dolly is one undo step");
    check(!document.TopologyDirty(), "moving the 3D Layout's camera does not modify the composition");
    {
        // 2D entities (Dev.exe sub_4B5C60, sub_48E630, sub_497770): the
        // handles, a move that snaps to the view's border, a scale by the
        // mouse's travel, cancel and undo.
        const EditResult made = CreateLayoutObject(document, LayoutObject::Frame2D);
        CK2dEntity *frame = CK2dEntity::Cast(context->GetObject(made.created));
        check(made.ok && frame, "a 2D frame: " + made.message);
        if (frame) {
            frame->SetPosition(Vx2DVector(100.0f, 80.0f));
            frame->SetSize(Vx2DVector(120.0f, 60.0f));
            check(view.Render(640, 480, pixels), "a frame with the 2D frame");
            VxRect source, rect;
            frame->GetExtents(source, rect);
            std::cout << "layout: 2d frame on screen " << rect.left << "," << rect.top << " " << rect.GetWidth() << " x "
                      << rect.GetHeight() << '\n';
            check(Handle2DAt(frame, rect.left + 2, rect.top + 2) == 1 && Handle2DAt(frame, rect.right - 2, rect.bottom - 2) == 2 &&
                      Handle2DAt(frame, (rect.left + rect.right) / 2, rect.bottom - 2) == 3 &&
                      Handle2DAt(frame, rect.right - 2, (rect.top + rect.bottom) / 2) == 4 &&
                      Handle2DAt(frame, (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2) == 0,
                  "the 2D handles");
            const std::vector<CK_ID> sprites{frame->GetID()};
            const int gx = static_cast<int>(rect.left + 30), gy = static_cast<int>(rect.top + 30);
            Transform2DGesture move;
            Snap2D borders;
            borders.mode = SNAP_SCREEN_BORDERS;
            borders.influence = 5;
            check(move.Begin(view, TransformAction::Translate, 3, false, borders, 10.0f, sprites, frame->GetID(), gx, gy),
                  "a 2D move begins");
            move.Move(gx + 30, gy + 10);
            Vx2DVector position;
            frame->GetPosition(position);
            check(std::fabs(position.x - 130.0f) < 0.01f && std::fabs(position.y - 90.0f) < 0.01f, "the 2D frame follows the mouse");
            move.Cancel();
            frame->GetPosition(position);
            check(std::fabs(position.x - 100.0f) < 0.01f, "cancel puts it back");
            // To 3 pixels from the left border: it snaps to the border.
            const size_t before = undo.UndoCount();
            check(move.Begin(view, TransformAction::Translate, 1, true, borders, 10.0f, sprites, frame->GetID(), gx, gy),
                  "a snapped 2D move begins");
            move.Move(gx - static_cast<int>(rect.left) + 3, gy + 40);
            VxRect moved;
            frame->GetRect(moved);
            std::cout << "layout: 2d frame snapped to " << moved.left << "," << moved.top << '\n';
            check(std::fabs(moved.left) < 0.01f && std::fabs(moved.top - rect.top) < 0.01f,
                  "Screen Borders snaps the left edge, the X axis only moves");
            check(move.End(&document) && (undo.UndoCount() - before) == 1, "the 2D move is one undo step");
            undo.Undo();
            frame->GetPosition(position);
            check(std::fabs(position.x - 100.0f) < 0.01f, "undo puts the 2D frame back");
            // The bottom-right handle: the size grows by the travel.
            Transform2DGesture scale;
            check(scale.Begin(view, TransformAction::Scale, 3, false, borders, 10.0f, sprites, frame->GetID(),
                              static_cast<int>(rect.right - 2), static_cast<int>(rect.bottom - 2)),
                  "a 2D scale begins");
            scale.Move(static_cast<int>(rect.right - 2) + 20, static_cast<int>(rect.bottom - 2) + 10);
            Vx2DVector size;
            frame->GetSize(size);
            std::cout << "layout: 2d frame scaled to " << size.x << " x " << size.y << '\n';
            check(std::fabs(size.x - 140.0f) < 0.05f && std::fabs(size.y - 70.0f) < 0.05f, "the 2D size follows the mouse");
            scale.Cancel();
        }
        if (made.ok) undo.Undo();
    }
    undo.Undo();
    check(Magnitude(position(perspective) - start) < 0.001f && Magnitude(position(target) - targetStart) < 0.001f,
          "undo restores the camera and its target");
    // Pan: 64 pixels right = 100 * 64 / 640 = 10 to the camera's left.
    VxVector right, upAxis, dirAxis;
    perspective->GetOrientation(&dirAxis, &upAxis, &right);
    check(gesture.Begin(view, CameraAction::Pan, steps, 320, 240, {}, false, false, false), "pan begins");
    gesture.Move(384, 240, false);
    const VxVector panned = position(perspective) - start;
    check(std::fabs(DotProduct(panned, right) + 10.0f) < 0.01f && Magnitude(position(target) - targetStart - panned) < 0.001f,
          "pan moves the camera and its target 10 along the camera's X");
    gesture.Cancel();
    // Orbit around the target: a quarter turn keeps the distance and the aim.
    const float distance = Magnitude(position(target) - start);
    check(gesture.Begin(view, CameraAction::Orbit, steps, 0, 240, {}, false, false, false), "orbit begins");
    gesture.Move(-static_cast<int>(640 * 1.5707964f), 240, false);
    view.AimViewCamera();
    VxVector orbitDir, orbitUp;
    perspective->GetOrientation(&orbitDir, &orbitUp);
    VxVector toTarget = position(target) - position(perspective);
    std::cout << "layout: orbit to " << position(perspective).x << "," << position(perspective).y << ","
              << position(perspective).z << '\n';
    check(std::fabs(Magnitude(toTarget) - distance) < 0.05f, "orbit keeps the distance to the target");
    toTarget.Normalize();
    orbitDir.Normalize();
    check(DotProduct(toTarget, orbitDir) > 0.999f && std::fabs(position(perspective).z) < 0.5f,
          "a quarter turn around the target, still aiming at it");
    gesture.Cancel();
    // Field of view: 64 pixels right = + 64 / 640.
    const float fov = perspective->GetFov();
    check(gesture.Begin(view, CameraAction::Fov, steps, 320, 240, {}, false, false, false), "field of view begins");
    gesture.Move(256, 240, false);
    check(std::fabs(perspective->GetFov() - fov - 0.1f) < 0.001f, "the field of view grows by 0.1");
    gesture.Cancel();
    check(std::fabs(perspective->GetFov() - fov) < 0.0001f, "cancel restores the field of view");
    // Orbit and Roll leave the Top View for the Orthographic View.
    view.SetCamera(view.ViewCamera(LayoutViewCamera::Top));
    check(gesture.Begin(view, CameraAction::Roll, steps, 320, 240, {}, false, false, false) &&
              view.Camera()->GetID() == view.ViewCamera(LayoutViewCamera::Orthographic),
          "roll on the Top View goes to the Orthographic View");
    gesture.Cancel();
    view.SetCamera(view.ViewCamera(LayoutViewCamera::Perspective));
    check(!ZoomOnObjects(view, {}, true, nullptr), "an empty scene has nothing to zoom on");
    // Reset Current Camera Settings (Dev.exe command 150).
    {
        const VxVector moved(30.0f, 40.0f, -70.0f), away(5.0f, 0.0f, 5.0f);
        perspective->SetPosition(&moved);
        target->SetPosition(&away);
        perspective->SetFov(1.2f);
        view.ResetCamera();
        view.AimViewCamera();
        VxVector dir, up;
        perspective->GetOrientation(&dir, &up);
        check(Magnitude(position(perspective) - VxVector(0.0f, 10.0f, -50.0f)) < 0.001f &&
                  Magnitude(position(target)) < 0.001f && std::fabs(perspective->GetFov() - 0.68940508f) < 0.0001f &&
                  dir.y < -0.1f && dir.z > 0.9f,
              "Reset puts the Perspective View back at 0,10,-50 aiming at its target at the origin");
        view.SetCamera(view.ViewCamera(LayoutViewCamera::Top));
        CKCamera *top = view.Camera();
        top->SetOrthographicZoom(0.5f);
        top->SetPosition(&moved);
        view.ResetCamera();
        top->GetOrientation(&dir, &up);
        check(Magnitude(position(top) - VxVector(0.0f, 1000.0f, 0.0f)) < 0.001f && dir.y < -0.999f && up.z > 0.999f &&
                  std::fabs(top->GetOrthographicZoom() - 0.007f) < 0.00001f,
              "Reset puts the Top View back above the origin, zoom 0.007");
        view.SetCamera(view.ViewCamera(LayoutViewCamera::Perspective));
    }
    // Flycam (Dev.exe sub_48CD30, sub_48CA50, sub_48C690, sub_48D130) on the Perspective View.
    {
        FlySteps fly; // constant speed: one second of each
        auto distance = [&] { return Magnitude(position(target) - position(perspective)); };
        auto aim = [&] {
            VxVector d = position(target) - position(perspective);
            d.Normalize();
            return d;
        };
        const float length = distance();
        const VxVector flyStart = position(perspective), aim0 = aim();
        Fly(view, FlyMode::Advance, 0.0f, 0.1f, 1.0f, fly, false, VxVector(0, 0, 0));
        check(std::fabs(DotProduct(position(perspective) - flyStart, aim0) - 10.0f) < 0.01f &&
                  std::fabs(distance() - length) < 0.01f,
              "Advance: 10 along the view, the target with it");
        view.ResetCamera();
        Fly(view, FlyMode::Look, 0.1f, 0.0f, 1.0f, fly, false, VxVector(0, 0, 0));
        check(Magnitude(position(perspective) - flyStart) < 0.001f &&
                  std::fabs(std::acos(std::clamp(DotProduct(aim(), aim0), -1.0f, 1.0f)) - 0.1f) < 0.01f,
              "Look: the camera stays, the view turns 0.1");
        view.ResetCamera();
        VxVector right, up, dir;
        perspective->GetOrientation(&dir, &up, &right);
        Fly(view, FlyMode::Strafe, -0.1f, 0.0f, 1.0f, fly, false, VxVector(0, 0, 0));
        const float strafed = DotProduct(position(perspective) - flyStart, right);
        check(std::fabs(strafed - 10.0f * Magnitude(flyStart) / 70.0f) < 0.01f && std::fabs(distance() - length) < 0.01f,
              "Strafe: along the camera's X by its distance / 70, the target with it");
        view.ResetCamera();
        Fly(view, FlyMode::Orbit, 0.5f, 0.0f, 1.0f, fly, false, VxVector(0, 0, 0));
        check(std::fabs(Magnitude(position(perspective)) - Magnitude(flyStart)) < 0.01f &&
                  Magnitude(position(perspective) - flyStart) > 1.0f,
              "Orbit: around the center at the same distance");
        view.ResetCamera();
        view.AimViewCamera();
    }
    // A 3D Frame is drawn with its representation (1, the first shape); 0 hides it.
    {
        const EditResult frame =
            CreateLayoutObject(document, LayoutObject::Frame3D, 0, view.ViewCamera(LayoutViewCamera::Perspective));
        check(frame.ok, "create a 3D Frame: " + frame.message);
        view.SetCamera(view.ViewCamera(LayoutViewCamera::Perspective));
        check(view.Render(640, 480, pixels), "a frame with a 3D Frame");
        const size_t shown = view.IconCount();
        // Made in front of the camera: its icon in the middle of the view, picked there.
        check(view.Pick(320, 240) == frame.created, "the 3D Frame picked by its icon in the middle of the view");
        view.SetRepresentations({{frame.created, 0}});
        check(view.Render(640, 480, pixels) && view.IconCount() + 1 == shown, "No Representation hides the 3D Frame");
        view.SetRepresentations({{frame.created, 2}});
        check(view.Render(640, 480, pixels) && view.IconCount() == shown, "another representation draws it");
        view.SetRepresentations({});
        std::cout << "layout: frame shapes";
        for (const std::string &shape : view.FrameShapeNames()) std::cout << ' ' << shape;
        std::cout << ", " << shown << " icons with the frame\n";
        check(view.FrameShapeNames().size() >= 2, "the frame shapes of interface.nmo");
        undo.Undo();
    }
    // The curve tools (Dev.exe sub_49ACC0, sub_49CD00, sub_49B400).
    {
        // CK's InsertControlPoint(prev, pt), which Dev's Add Points (right
        // button) and Insert Points use: where pt goes.
        CKCurve *scratch = CKCurve::Cast(context->CreateObject(CKCID_CURVE, (CKSTRING) "scratch"));
        CKCurvePoint *a = CKCurvePoint::Cast(context->CreateObject(CKCID_CURVEPOINT, (CKSTRING) "a"));
        CKCurvePoint *b = CKCurvePoint::Cast(context->CreateObject(CKCID_CURVEPOINT, (CKSTRING) "b"));
        CKCurvePoint *c = CKCurvePoint::Cast(context->CreateObject(CKCID_CURVEPOINT, (CKSTRING) "c"));
        scratch->AddControlPoint(a);
        scratch->AddControlPoint(b);
        scratch->InsertControlPoint(b, c);
        int at = -1;
        for (int i = 0; i < scratch->GetControlPointCount(); ++i)
            if (scratch->GetControlPoint(i) == c) at = i;
        std::cout << "layout: InsertControlPoint(second, point) puts it at " << at << " of "
                  << scratch->GetControlPointCount() << '\n';
        check(at == 1, "CKCurve::InsertControlPoint puts the point before prev");
        for (CKObject *object : std::initializer_list<CKObject *>{c, b, a, scratch}) context->DestroyObject(object);

        const EditResult made = CreateLayoutObject(document, LayoutObject::Curve, 0, perspective->GetID());
        CKCurve *curve = CKCurve::Cast(context->GetObject(made.created));
        check(made.ok && curve, "create a curve: " + made.message);
        if (curve) {
            const VxVector spots[3] = {VxVector(0, 0, 0), VxVector(10, 0, 0), VxVector(10, 0, 10)};
            const size_t undoCurve = undo.UndoCount();
            for (const VxVector &spot : spots)
                check(AddCurvePointAt(document, curve->GetID(), curve->GetControlPointCount(), spot, false, "Add Points")
                          .ok,
                      "add a curve point");
            check(curve->GetControlPointCount() == 3 && (undo.UndoCount() - undoCurve) == 3, "three points, three steps");
            bool placed = curve->GetControlPointCount() == 3;
            for (int i = 0; placed && i < 3; ++i) {
                CKCurvePoint *point = curve->GetControlPoint(i);
                VxVector at, scale;
                point->GetPosition(&at);
                point->GetScale(&scale, FALSE);
                char name[256];
                std::snprintf(name, sizeof(name), "%s_Point%04d", curve->GetName(), i + 1);
                placed = Magnitude(at - spots[i]) < 0.001f && std::fabs(scale.x - 0.4f) < 0.001f && point->GetName() &&
                         std::strcmp(point->GetName(), name) == 0;
            }
            check(placed, "the points where they were put, 0.4 in the world, named <curve>_Point0001...");
            {
                // An open spline, fitting 0: through its points, after the base moved.
                float worst = 0.0f;
                for (int i = 0; i < curve->GetControlPointCount(); ++i) {
                    VxVector p;
                    curve->GetControlPoint(i)->GetPosition(&p);
                    float best = 1.0e30f;
                    for (float t = 0.0f; t <= 1.0f; t += 0.0005f) {
                        VxVector at;
                        curve->GetPos(t, &at);
                        best = std::min(best, Magnitude(at - p));
                    }
                    worst = std::max(worst, best);
                }
                std::cout << "layout: new curve, fitting " << curve->GetFittingCoeff() << ", points off by " << worst << '\n';
                check(worst < 0.1f, "the curve passes through its points");
            }
            VxVector base;
            curve->GetPosition(&base);
            check(Magnitude(base - VxVector(20.0f / 3.0f, 0.0f, 10.0f / 3.0f)) < 0.001f, "the curve's base at their center");
            // The guide is the X+Z plane: the ray through the view's center meets it at the target.
            view.ResetCamera();
            view.AimViewCamera();
            check(view.Render(640, 480, pixels), "a frame with the curve");
            VxVector hit;
            const VxRect rect = view.ViewRect();
            const VxMatrix guide = view.GuideMatrix();
            check(view.RayOnPlane(static_cast<int>(rect.GetCenter().x), static_cast<int>(rect.GetCenter().y),
                                  VxVector(guide[3][0], guide[3][1], guide[3][2]),
                                  VxVector(guide[1][0], guide[1][1], guide[1][2]), hit) &&
                      Magnitude(hit) < 0.5f,
                  "the ray through the view's center meets the guide at the origin");
            // A point of the curve on screen is found on it.
            VxVector middle, screen, found;
            curve->GetPos(0.5f, &middle);
            ScreenOfWorldPoint(view.RenderContext(), &screen, middle);
            float along = -1.0f;
            check(view.CurveAt(static_cast<int>(screen.x), static_cast<int>(screen.y), curve->GetID(), found, &along) &&
                      Magnitude(found - middle) < 1.0f && std::fabs(along - 0.5f) < 0.05f,
                  "the curve under the mouse");
            // Delete the middle point; undo gives it back.
            const CK_ID middlePoint = curve->GetControlPoint(1)->GetID();
            check(EditCurvePoints(document, curve->GetID(),
                                  {curve->GetControlPoint(0)->GetID(), curve->GetControlPoint(2)->GetID()}, middlePoint,
                                  "Delete Points")
                          .ok &&
                      curve->GetControlPointCount() == 2 && document.IsDiscarded(middlePoint),
                  "delete a point");
            undo.Undo();
            check(curve->GetControlPointCount() == 3 && curve->GetControlPoint(1)->GetID() == middlePoint &&
                      !document.IsDiscarded(middlePoint),
                  "undo gives the point back");
            for (int i = 0; i < 3; ++i) undo.Undo();
            check(curve->GetControlPointCount() == 0, "undo removes the points added");
        }
        undo.Undo();
    }

    if (!fs::exists(path)) return failures ? 1 : 0;
    check(document.Open(path, &error), "open: " + error);
    check(view.Started(), "the render context survives ClearAll");
    view.CompositionChanged();
    level = context->GetCurrentLevel();
    check(level && level->GetRenderContextCount() >= 1 && level->GetRenderContext(0) == view.RenderContext(),
          "the opened composition's main render context");
    check(context->GetObject(view.ViewCamera(LayoutViewCamera::Perspective)) != nullptr, "the views are made again");
    const std::vector<CK_ID> cameras = view.Cameras();
    std::cout << "layout: " << cameras.size() - LAYOUT_VIEW_CAMERA_COUNT << " composition cameras\n";
    {
        // The composition's curves are updated when it opens (Dev.exe
        // sub_49DC50 -> sub_4AC040): their line meshes are drawn.
        const XObjectPointerArray &curves = context->GetObjectListByType(CKCID_CURVE, TRUE);
        for (int i = 0; i < curves.Size(); ++i)
            if (CKCurve *curve = CKCurve::Cast(curves[i])) {
                if (curve->GetControlPointCount() < 2) continue;
                CKMesh *line = curve->GetCurrentMesh();
                std::cout << "layout: curve " << (curve->GetName() ? curve->GetName() : "") << ", "
                          << curve->GetControlPointCount() << " points, " << (curve->IsOpen() ? "open" : "closed")
                          << ", fitting " << curve->GetFittingCoeff() << ", line mesh "
                          << (line ? line->GetVertexCount() : 0) << " vertices\n";
                check(line && line->GetVertexCount() > 0, "a curve of the composition has its line mesh");
            }
    }
    for (size_t i = LAYOUT_VIEW_CAMERA_COUNT; i < cameras.size(); ++i) {
        view.SetCamera(cameras[i]);
        break;
    }
    check(view.Render(640, 480, pixels), "a frame of the composition");
    WriteFrame((fs::path(outputDirectory) / "file.bmp").string(), view.Width(), view.Height(), pixels);
    {
        // Picking (Dev.exe sub_49F940): the object whose box center is seen
        // at a point is picked there, unless another is in front; a
        // rectangle over the whole view takes every picked one.
        CKRenderContext *renderContext = view.RenderContext();
        int picked = 0, tried = 0;
        std::vector<CK_ID> seen;
        const XObjectPointerArray &objects = context->GetObjectListByType(CKCID_3DOBJECT, TRUE);
        for (int i = 0; i < objects.Size(); ++i) {
            CK3dEntity *entity = CK3dEntity::Cast(objects[i]);
            if (!entity || !entity->GetCurrentMesh() || !entity->IsVisible() || entity->IsPrivate()) continue;
            VxVector center = entity->GetBoundingBox(FALSE).GetCenter(), screen;
            ScreenOfWorldPoint(renderContext, &screen, center);
            const VxRect rect = view.ViewRect();
            if (screen.z <= 0.0f || screen.z >= 1.0f || !rect.IsInside(Vx2DVector(screen.x, screen.y))) continue;
            ++tried;
            const CK_ID id = view.Pick(static_cast<int>(screen.x), static_cast<int>(screen.y));
            if (id) {
                ++picked;
                if (std::find(seen.begin(), seen.end(), id) == seen.end()) seen.push_back(id);
            }
        }
        const std::vector<CK_ID> all = view.RectPick(VxRect(0.0f, 0.0f, 640.0f, 480.0f), true);
        std::cout << "layout: " << view.IconCount() << " icons" << std::endl;
        check(view.IconCount() > 0, "the composition's camera, light and curve points are drawn as icons");
        // The icons are picked: an icon's owner (a light, a curve point:
        // nothing else in front of it in this sample) at its position.
        int iconsTried = 0, iconsPicked = 0;
        for (CK_CLASSID cid : {CKCID_LIGHT, CKCID_CURVEPOINT}) {
            const XObjectPointerArray &owners = context->GetObjectListByType(cid, TRUE);
            for (int i = 0; i < owners.Size(); ++i) {
                CK3dEntity *owner = CK3dEntity::Cast(owners[i]);
                if (!owner || owner->IsPrivate() || owner->GetCurrentMesh()) continue;
                VxVector position, screen;
                owner->GetPosition(&position);
                ScreenOfWorldPoint(renderContext, &screen, position);
                if (screen.z <= 0.0f || screen.z >= 1.0f || !view.ViewRect().IsInside(Vx2DVector(screen.x, screen.y))) continue;
                ++iconsTried;
                if (view.Pick(static_cast<int>(screen.x), static_cast<int>(screen.y)) == owner->GetID()) ++iconsPicked;
            }
        }
        std::cout << "layout: icons picked " << iconsPicked << " of " << iconsTried << '\n';
        check(iconsTried > 0 && iconsPicked > 0, "icons are picked at their owners' positions");
        // The icons are placed without drawing (PlaceIcons): as the frame drew
        // them, each box around its owner, and with the camera moved, before a
        // frame, where the camera shows them now.
        {
            const VxRect rect = view.ViewRect();
            CKCamera *camera = view.Camera();
            const ViewProjection projection = ViewProjection::Of(camera, rect);
            const std::vector<LayoutView::IconHit> placed = view.PlaceIcons(projection, camera->GetID());
            const std::vector<LayoutView::IconHit> &drawnIcons = view.DrawnIcons();
            auto sameBox = [](const VxRect &a, const VxRect &b) {
                return std::fabs(a.left - b.left) < 0.01f && std::fabs(a.right - b.right) < 0.01f &&
                       std::fabs(a.top - b.top) < 0.01f && std::fabs(a.bottom - b.bottom) < 0.01f;
            };
            bool same = placed.size() == drawnIcons.size();
            for (size_t i = 0; same && i < placed.size(); ++i)
                same = placed[i].owner == drawnIcons[i].owner && placed[i].mesh == drawnIcons[i].mesh &&
                       sameBox(placed[i].rect, drawnIcons[i].rect);
            check(same, "the icons placed are the icons drawn");
            int boxes = 0, around = 0;
            for (const LayoutView::IconHit &icon : placed) {
                CK3dEntity *owner = CK3dEntity::Cast(context->GetObject(icon.owner));
                if (!owner) continue;
                VxVector position;
                owner->GetPosition(&position);
                const VxVector screen = projection.Project(position);
                if (screen.z <= 0.0f || screen.z >= 1.0f) continue;
                ++boxes;
                if (icon.rect.left <= screen.x && screen.x <= icon.rect.right && icon.rect.top <= screen.y &&
                    screen.y <= icon.rect.bottom)
                    ++around;
                else
                    std::cout << "layout: the icon of " << (owner->GetName() ? owner->GetName() : "") << " is not around it\n";
            }
            std::cout << "layout: " << placed.size() << " icons placed, " << around << " of " << boxes
                      << " around their owners\n";
            check(placed.size() >= 2 && boxes > 0 && around == boxes, "every icon's box is around its owner");
            // The camera moved, no frame drawn.
            VxVector at, direction, up, right;
            camera->GetPosition(&at);
            camera->GetOrientation(&direction, &up, &right);
            const VxVector moved = at + right * 3.0f;
            camera->SetPosition(&moved);
            const std::vector<LayoutView::IconHit> after = view.PlaceIcons(ViewProjection::Of(camera, rect), camera->GetID());
            bool followed = after.size() == placed.size(), pickedThere = false;
            for (size_t i = 0; followed && i < after.size(); ++i) {
                followed = after[i].owner == placed[i].owner && !sameBox(after[i].rect, placed[i].rect);
                const float cx = (after[i].rect.left + after[i].rect.right) * 0.5f,
                            cy = (after[i].rect.top + after[i].rect.bottom) * 0.5f;
                if (!pickedThere && rect.IsInside(Vx2DVector(cx, cy))) {
                    const std::vector<CK_ID> under = view.RectPick(VxRect(cx - 1.0f, cy - 1.0f, cx + 1.0f, cy + 1.0f), true);
                    pickedThere = std::find(under.begin(), under.end(), after[i].owner) != under.end();
                }
            }
            check(followed, "the icons follow the camera before the next frame");
            check(pickedThere, "an icon is picked where the moved camera shows it, before the next frame");
            camera->SetPosition(&at);
        }
        std::cout << "layout: picked " << picked << " of " << tried << " objects in view, rectangle "
                  << all.size() << '\n';
        check(tried == 0 || picked > 0, "objects are picked at their box centers");
        // CK's RectPick leaves out what covers the whole rectangle (Dev uses it as is).
        bool rendered = !all.empty();
        for (CK_ID id : all) rendered = rendered && CKRenderObject::Cast(context->GetObject(id)) != nullptr;
        check(rendered, "a rectangle over the view takes render objects");
        // The same rectangle gives the same objects every time (CK2_3D 2.1 fills a CKObjectArray: the wrong array
        // type corrupted the heap and gave other sizes at each call).
        bool stable = true;
        for (int round = 0; round < 50; ++round) stable = stable && view.RectPick(VxRect(0.0f, 0.0f, 640.0f, 480.0f), true) == all;
        check(stable, "a rectangle gives the same objects every time");
        check(view.Pick(-10, -10) == 0, "nothing is picked outside the view");
        view.SetSelection(seen);
        check(view.Render(640, 480, pixels), "a frame with the selection");
        WriteFrame((fs::path(outputDirectory) / "selection.bmp").string(), view.Width(), view.Height(), pixels);
        view.SetSelection({});

        // Transformation tools on the first picked object.
        CK3dEntity *moved = seen.empty() ? nullptr : CK3dEntity::Cast(context->GetObject(seen.front()));
        if (moved) {
            VxVector center = moved->GetBoundingBox(FALSE).GetCenter(), screen, grabbed;
            ScreenOfWorldPoint(renderContext, &screen, center);
            const int sx = static_cast<int>(screen.x), sy = static_cast<int>(screen.y);
            const CK_ID under = view.Pick(sx, sy, false, &grabbed);
            const std::vector<CK_ID> selection{moved->GetID()};
            VxVector before;
            moved->GetPosition(&before);
            const size_t undoCount = undo.UndoCount();
            TransformGesture transform;
            TransformSettings settings; // X+Z in the world
            TransformSteps transformSteps;
            // Sticky translate: the grabbed point stays under the mouse on the X+Z plane.
            check(transform.Begin(view, TransformAction::Translate, settings, transformSteps, 0, selection, sx, sy,
                                  under == moved->GetID() ? &grabbed : nullptr),
                  "translate begins");
            transform.Move(sx + 40, sy);
            VxVector after;
            moved->GetPosition(&after);
            std::cout << "layout: translate from " << before.x << "," << before.y << "," << before.z << " to " << after.x
                      << "," << after.y << "," << after.z << '\n';
            check(Magnitude(after - before) > 0.01f && std::fabs(after.y - before.y) < 0.001f,
                  "translating on the X+Z plane keeps the height");
            transform.Cancel();
            moved->GetPosition(&after);
            check(Magnitude(after - before) < 0.001f, "cancel restores the position");
            check(transform.Begin(view, TransformAction::Translate, settings, transformSteps, 0, selection, sx, sy, nullptr),
                  "translate begins again");
            transform.Move(sx + 40, sy);
            check(transform.End(&document) && (undo.UndoCount() - undoCount) == 1, "the move is one undo step");
            undo.Undo();
            moved->GetPosition(&after);
            check(Magnitude(after - before) < 0.001f, "undo restores the position");
            // Snap & Align (Dev.exe sub_48EED0): World Sections puts the moved
            // axes on a corner of the World Step cell, the height stays.
            TransformSettings snapping = settings;
            snapping.snap = true;
            TransformSteps sections = transformSteps;
            sections.snapMode = SNAP_WORLD_SECTIONS;
            sections.worldStep = VxVector(2.0f, 2.0f, 2.0f);
            check(transform.Begin(view, TransformAction::Translate, snapping, sections, 0, selection, sx, sy,
                                  under == moved->GetID() ? &grabbed : nullptr),
                  "snapped translate begins");
            transform.Move(sx + 40, sy);
            moved->GetPosition(&after);
            std::cout << "layout: snapped to " << after.x << "," << after.y << "," << after.z << '\n';
            auto onStep = [](float value) { return std::fabs(value / 2.0f - std::round(value / 2.0f)) < 0.0005f; };
            check(onStep(after.x) && onStep(after.z) && std::fabs(after.y - before.y) < 0.001f,
                  "World Sections snaps X and Z to the World Step");
            transform.Cancel();
            // Objects Axis: the object goes to the axis of the entity under the mouse.
            CK3dEntity *other = nullptr;
            for (CK_ID id : seen)
                if (id != moved->GetID() && CK3dEntity::Cast(context->GetObject(id))) other = CK3dEntity::Cast(context->GetObject(id));
            if (other) {
                VxVector otherCenter = other->GetBoundingBox(FALSE).GetCenter(), otherScreen, otherAxis;
                ScreenOfWorldPoint(renderContext, &otherScreen, otherCenter);
                other->GetPosition(&otherAxis);
                TransformSteps axis = transformSteps;
                axis.snapMode = SNAP_OBJECTS_AXIS;
                check(transform.Begin(view, TransformAction::Translate, snapping, axis, 0, selection, sx, sy, nullptr),
                      "axis-snapped translate begins");
                const int ox = static_cast<int>(otherScreen.x), oy = static_cast<int>(otherScreen.y);
                CKObject *hit = context->GetObject(view.Pick(ox, oy, false, nullptr));
                transform.Move(ox, oy);
                moved->GetPosition(&after);
                std::cout << "layout: axis snap over " << (hit && hit->GetName() ? hit->GetName() : "?") << " to "
                          << after.x << "," << after.y << "," << after.z << '\n';
                if (hit == other)
                    check(std::fabs(after.x - otherAxis.x) < 0.001f && std::fabs(after.z - otherAxis.z) < 0.001f,
                          "Objects Axis snaps X and Z to the entity's axis");
                transform.Cancel();
            }
            // Rotate about Y: 64 pixels of 640 = 18 degrees.
            TransformSettings rotation;
            rotation.axes = 2;
            rotation.referential = 1;
            // The local axis farthest from Y, seen from above, before and after.
            auto flat = [&](int axis) {
                const VxMatrix &world = moved->GetWorldMatrix();
                VxVector v(world[axis][0], 0.0f, world[axis][2]);
                v.Normalize();
                return v;
            };
            const VxMatrix &start = moved->GetWorldMatrix();
            const int axis = std::fabs(start[0][1]) < std::fabs(start[2][1]) ? 0 : 2;
            const VxVector dir0 = flat(axis);
            check(transform.Begin(view, TransformAction::Rotate, rotation, transformSteps, 0, selection, sx, sy, nullptr),
                  "rotate begins");
            transform.Move(sx + 64, sy);
            const VxVector dir1 = flat(axis);
            const float turned = std::acos(std::clamp(DotProduct(dir0, dir1), -1.0f, 1.0f)) * 57.29578f;
            std::cout << "layout: rotate turned " << turned << " degrees\n";
            check(std::fabs(turned - 18.0f) < 0.5f, "rotate turns 18 degrees about Y");
            transform.Cancel();
            // Scale uniformly: 48 pixels up of 480 = 1 + 0.1 * 5.
            VxVector scale0, scale1;
            moved->GetScale(&scale0);
            check(transform.Begin(view, TransformAction::Scale, rotation, transformSteps, 0, selection, sx, sy, nullptr),
                  "scale begins");
            transform.Move(sx, sy - 48);
            moved->GetScale(&scale1);
            std::cout << "layout: scale " << scale0.x << " to " << scale1.x << '\n';
            check(std::fabs(scale1.x / scale0.x - 1.5f) < 0.01f && std::fabs(scale1.y / scale0.y - 1.5f) < 0.01f,
                  "uniform scaling by 1.5");
            transform.Cancel();
            moved->GetScale(&scale1);
            check(Magnitude(scale1 - scale0) < 0.001f, "cancel restores the scale");

            // The shortcut menu's objects (Dev.exe sub_49F640): the entity and its mesh.
            const LayoutPickInfo info = view.PickInfo(sx, sy);
            check((info.entity3d == moved->GetID() || info.entity3d == under) && !info.entity2d,
                  "the menu's 3D entity is the one under the mouse");
            if (info.entity3d == moved->GetID())
                check(info.mesh == (moved->GetCurrentMesh() ? moved->GetCurrentMesh()->GetID() : 0),
                      "the menu's mesh is the entity's");
            std::cout << "layout: menu objects entity " << info.entity3d << " mesh " << info.mesh << " material "
                      << info.material << " texture " << info.texture << '\n';

            // Duplicate (Dev.exe sub_4B0780 2 and 3): copies named ".Copy" in
            // the original's scenes; undo takes them out, redo puts them back.
            CKScene *levelScene = context->GetCurrentLevel()->GetLevelScene();
            const size_t undoDuplicate = undo.UndoCount();
            std::vector<std::pair<CK_ID, CK_ID>> copies;
            const EditResult duplicated = DuplicateObjects(document, selection, &copies);
            check(duplicated.ok && copies.size() == 1 && (undo.UndoCount() - undoDuplicate) == 1,
                  "duplicate is one undo step: " + duplicated.message);
            CK3dEntity *copy = copies.empty() ? nullptr : CK3dEntity::Cast(context->GetObject(copies.front().second));
            check(copy && copy != moved, "the copy is a new entity");
            if (copy) {
                const std::string expected = std::string(moved->GetName() ? moved->GetName() : "") + ".Copy";
                std::cout << "layout: duplicate " << (copy->GetName() ? copy->GetName() : "") << " in "
                          << copy->GetSceneInCount() << " scene(s), original in " << moved->GetSceneInCount() << '\n';
                check(copy->GetName() && expected == copy->GetName(), "the copy's name ends with .Copy");
                check(levelScene->IsObjectHere(copy) && copy->GetSceneInCount() == moved->GetSceneInCount(),
                      "the copy is in the level and the original's scenes");
                for (int s = 0; s < moved->GetScriptCount() && s < copy->GetScriptCount(); ++s)
                    check(!moved->GetScript(s)->GetInterfaceChunk() ||
                              copy->GetScript(s)->GetInterfaceChunk() != moved->GetScript(s)->GetInterfaceChunk(),
                          "a copied Script has its own InterfaceChunk");
                undo.Undo();
                check(!levelScene->IsObjectHere(copy) && document.IsDiscarded(copy->GetID()),
                      "undo takes the copy out of the level");
                undo.Redo();
                check(levelScene->IsObjectHere(copy) && !document.IsDiscarded(copy->GetID()),
                      "redo puts it back");
                undo.Undo();
            }
            // A material dropped on a face (Dev.exe sub_4C0A30): the mesh's faces
            // of that material take the new one; undo gives them back.
            if (CKMesh *mesh = moved->GetCurrentMesh()) {
                CKMaterial *old = CKMaterial::Cast(context->GetObject(info.material));
                CKMaterial *replacement = nullptr;
                const XObjectPointerArray &materials = context->GetObjectListByType(CKCID_MATERIAL, TRUE);
                for (int i = 0; i < materials.Size() && !replacement; ++i)
                    if (materials[i] != old && !materials[i]->IsPrivate()) replacement = CKMaterial::Cast(materials[i]);
                if (old && replacement && info.entity3d == moved->GetID()) {
                    int had = 0;
                    for (int f = 0; f < mesh->GetFaceCount(); ++f) had += mesh->GetFaceMaterial(f) == old ? 1 : 0;
                    const EditResult replaced =
                        ReplaceMeshMaterial(document, mesh->GetID(), old->GetID(), replacement->GetID());
                    int now = 0;
                    for (int f = 0; f < mesh->GetFaceCount(); ++f) now += mesh->GetFaceMaterial(f) == replacement ? 1 : 0;
                    std::cout << "layout: material drop " << had << " face(s) of " << (old->GetName() ? old->GetName() : "")
                              << " to " << (replacement->GetName() ? replacement->GetName() : "") << '\n';
                    check(replaced.ok && now >= had && had > 0, "the faces take the dropped material");
                    undo.Undo();
                    int back = 0;
                    for (int f = 0; f < mesh->GetFaceCount(); ++f) back += mesh->GetFaceMaterial(f) == old ? 1 : 0;
                    check(back == had, "undo gives the faces their material back");
                }
            }
            // The Copy Options (Dev.exe sub_5BDD60): Full copies the mesh, None shares it.
            if (moved->GetCurrentMesh()) {
                for (CK_DEPENDENCIES_FLAGS mode : {CK_DEPENDENCIES_FULL, CK_DEPENDENCIES_NONE}) {
                    CKDependencies dependencies = *CKGetDefaultClassDependencies(CK_DEPENDENCIES_COPY);
                    dependencies.m_Flags = mode;
                    std::vector<std::pair<CK_ID, CK_ID>> made;
                    const EditResult withOptions = DuplicateObjects(document, selection, &made, &dependencies);
                    CK3dEntity *other = made.empty() ? nullptr : CK3dEntity::Cast(context->GetObject(made.front().second));
                    const bool shared = other && other->GetCurrentMesh() == moved->GetCurrentMesh();
                    std::cout << "layout: copy options " << (mode == CK_DEPENDENCIES_FULL ? "full" : "none")
                              << (shared ? " shares" : " copies") << " the mesh\n";
                    check(withOptions.ok && other && shared == (mode == CK_DEPENDENCIES_NONE),
                          "the copy options choose whether the mesh is copied");
                    if (withOptions.ok) undo.Undo();
                }
            }

            // Selection Groups (Dev.exe sub_4B95C0 0x7E8, sub_4C3D10): a group of
            // the selection, private and not saved, in the level and the scene;
            // the combo matches it with the selection; undo takes it out.
            CKScene *groupScene = view.Scene();
            const EditResult grouped =
                CreateSelectionGroup(document, groupScene ? groupScene->GetID() : 0, "Selection Group Test", selection);
            CKGroup *group = CKGroup::Cast(context->GetObject(grouped.created));
            check(grouped.ok && group, "a selection group is made: " + grouped.message);
            if (group) {
                check((group->GetObjectFlags() & 0x23) == 0x23, "the group has Dev's interface flags 0x23");
                const XObjectPointerArray &levelGroups = context->GetCurrentLevel()->ComputeObjectList(CKCID_GROUP);
                bool inLevel = false;
                for (int i = 0; i < levelGroups.Size(); ++i) inLevel |= levelGroups[i] == group;
                check(inLevel && groupScene->IsObjectHere(group), "the group is in the level and the scene");
                const std::vector<CK_ID> groups = SceneGroups(context, groupScene);
                check(std::find(groups.begin(), groups.end(), group->GetID()) != groups.end(), "the scene lists the group");
                check(GroupSelection(context, group->GetID()) == selection, "choosing the group selects its entities");
                check(MatchingGroup(context, groups, selection) == group->GetID(), "the selection matches the group");
                check(MatchingGroup(context, groups, {}) == 0, "an empty selection matches no group");
                undo.Undo();
                const std::vector<CK_ID> after = SceneGroups(context, groupScene);
                check(std::find(after.begin(), after.end(), group->GetID()) == after.end(), "undo takes the group out of the scene");
                undo.Redo();
            }

            // The 3D Layout Explorer's types (Dev.exe sub_59EFF0, sub_59EC90).
            const auto listed = ExplorerObjects(context, view.Scene(), EXPLORER_ALL);
            std::map<unsigned, int> byType;
            bool privateListed = false;
            for (const auto &entry : listed) {
                ++byType[entry.second];
                CKObject *object = context->GetObject(entry.first);
                privateListed |= !object || object->IsPrivate() || view.IsInterfaceObject(entry.first);
            }
            std::cout << "layout: explorer lights " << byType[EXPLORER_LIGHTS] << " cameras " << byType[EXPLORER_CAMERAS]
                      << " 3d objects " << byType[EXPLORER_3D_OBJECTS] << " curves " << byType[EXPLORER_CURVES]
                      << " frames " << byType[EXPLORER_FRAMES] << " scripted " << byType[EXPLORER_SCRIPTED] << '\n';
            check(!listed.empty() && !privateListed, "the explorer lists the scene's objects, no private one");
            check(ExplorerTypeOf(moved, EXPLORER_ALL) == EXPLORER_3D_OBJECTS, "a 3D object is listed as one");
            check(SelectedTypes(context, selection) == EXPLORER_3D_OBJECTS ||
                      SelectedTypes(context, selection) == (EXPLORER_3D_OBJECTS | EXPLORER_SCRIPTED),
                  "the selected types of a 3D object");
            // With 3D objects hidden a scripted one falls to Scripted Objects.
            const unsigned hidden = ExplorerTypeOf(moved, EXPLORER_ALL & ~EXPLORER_3D_OBJECTS);
            check(hidden == (moved->GetScriptCount() ? EXPLORER_SCRIPTED : 0u), "hidden 3D objects leave the list");
            for (const auto &entry : listed) {
                CKObject *object = context->GetObject(entry.first);
                if (CKIsChildClassOf(object, CKCID_CAMERA))
                    check(entry.second == EXPLORER_CAMERAS, "cameras are listed as cameras");
                if (CKIsChildClassOf(object, CKCID_CURVE))
                    check(entry.second == EXPLORER_CURVES, "curves are listed as curves");
            }
        }
    }

    // Dev's Hierarchy Manager (Dev.exe sub_565180, sub_565520, sub_5778A0).
    // (It needs the composition the 3D Layout shows: with the sample only
    // opened, LayoutObjectsFile, the 3D Root holds nothing; not found why.)
    {
        HierarchyOptions options;
        const std::vector<HierarchyItem> roots = BuildHierarchy(context, options);
        check(roots.size() == 3 && roots[0].name == "2D Background Root" && roots[1].name == "2D Foreground Root" &&
                  roots[2].name == "3D Root",
              "the Hierarchy Manager's roots in Dev's order");
        const HierarchyItem &root3d = roots[2];
        bool sorted = true, interfaceShown = false, parented = false;
        for (size_t i = 1; i < root3d.children.size(); ++i)
            if (_stricmp(root3d.children[i - 1].name.c_str(), root3d.children[i].name.c_str()) > 0) sorted = false;
        for (const HierarchyItem &item : root3d.children) {
            CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(item.object));
            if (!entity || entity->IsPrivate()) interfaceShown = true;
            if (entity && entity->GetParent()) parented = true;
        }
        check(!root3d.children.empty() && sorted, "the 3D Root's entities by name");
        check(!interfaceShown && !parented, "the 3D Root holds the composition's entities without a parent");
        // Meshes show under their entities with the toolbar's button, materials under meshes.
        options.meshes = true;
        const std::vector<HierarchyItem> meshed = BuildHierarchy(context, options);
        bool mesh = false, material = false;
        std::function<void(const HierarchyItem &)> walk = [&](const HierarchyItem &item) {
            if (item.resource && CKIsChildClassOf(item.cid, CKCID_MESH)) mesh = true;
            if (item.resource && CKIsChildClassOf(item.cid, CKCID_MATERIAL)) material = true;
            for (const HierarchyItem &child : item.children) walk(child);
        };
        for (const HierarchyItem &item : meshed) walk(item);
        check(mesh && !material, "Show/Hide Meshes adds the meshes alone");
        options.materials = true;
        mesh = material = false;
        for (const HierarchyItem &item : BuildHierarchy(context, options)) walk(item);
        check(mesh && material, "Show/Hide Materials adds the meshes' materials");
        // Initial Conditions on Hierarchy take the entity and what is below it.
        for (const HierarchyItem &item : root3d.children)
            if (CK3dEntity *entity = CK3dEntity::Cast(context->GetObject(item.object)))
                if (entity->GetChildrenCount()) {
                    const std::vector<CK_ID> below = HierarchyOf(context, item.object);
                    check(below.size() > 1 && below.front() == item.object, "the hierarchy of an entity starts with it");
                    break;
                }
    }

    // The 3D Layout's answer to the target button (Dev.exe sub_4C18A0).
    {
        const ObjectChoices entities = ObjectsOfClass(document, CKCID_3DOBJECT, true, "");
        CK3dEntity *entity = nullptr;
        for (const auto &choice : entities)
            if (!entity && choice.first) entity = CK3dEntity::Cast(context->GetObject(choice.first));
        if (entity) {
            LayoutPickInfo info;
            info.entity3d = entity->GetID();
            CKMesh *mesh = entity->GetCurrentMesh();
            info.mesh = mesh ? mesh->GetID() : 0;
            CKMaterial *material = mesh && mesh->GetMaterialCount() ? mesh->GetMaterial(0) : nullptr;
            info.material = material ? material->GetID() : 0;
            check(LayoutPickObject(context, info, CKCID_3DENTITY, 0) == entity->GetID() &&
                      LayoutPickObject(context, info, CKCID_BEOBJECT, 0) == entity->GetID(),
                  "the view gives its 3D entity to the 3D classes");
            check(LayoutPickObject(context, info, CKCID_MESH, 0) == info.mesh &&
                      LayoutPickObject(context, info, CKCID_MATERIAL, 0) == info.material &&
                      LayoutPickObject(context, info, CKCID_2DENTITY, 0) == 0 &&
                      LayoutPickObject(context, info, CKCID_GROUP, 0) == 0,
                  "the view gives the mesh and material met, nothing to other classes");
            CKScene *scene = context->GetCurrentScene();
            check(LayoutPickObject(context, info, CKCID_SCENE, 0) == (scene ? scene->GetID() : 0) &&
                      LayoutPickObject(context, info, CKCID_LEVEL, 0) == context->GetCurrentLevel()->GetID(),
                  "the view gives the current scene and level");
            const ObjectChoices cameras = ObjectsOfClass(document, CKCID_CAMERA, true, "");
            for (const auto &camera : cameras)
                if (camera.first) {
                    check(LayoutPickObject(context, LayoutPickInfo(), CKCID_CAMERA, camera.first) == camera.first,
                          "the view gives the camera shown");
                    break;
                }
        }
    }

    {
        // The input manager's mouse position is the cursor less the render
        // context's window rectangle: the hidden window follows the view.
        view.SetScreenPosition(123, 45);
        VxRect rect;
        rc->GetWindowRect(rect, TRUE);
        std::cout << "layout: window rectangle " << rect.left << ' ' << rect.top << ' ' << rect.GetWidth() << ' '
                  << rect.GetHeight() << '\n';
        check(rect.left == 123.0f && rect.top == 45.0f && static_cast<int>(rect.GetWidth()) == view.Width(),
              "the render context's window is where the view is");
        // Player Mode's clicks: the 3D entity under the mouse, else the 2D
        // entity (both made in the scene for the check).
        CKScene *scene = context->GetCurrentScene();
        if (!scene && context->GetCurrentLevel()) scene = context->GetCurrentLevel()->GetLevelScene();
        CK3dEntity *entity = CK3dEntity::Cast(context->CreateObject(CKCID_3DOBJECT, (CKSTRING) "Clicked"));
        CK2dEntity *sprite = CK2dEntity::Cast(context->CreateObject(CKCID_2DENTITY, (CKSTRING) "Clicked 2D"));
        if (scene) {
            scene->AddObjectToScene(entity);
            scene->AddObjectToScene(sprite);
        }
        LayoutPickInfo info;
        check(LayoutSendClick(context, info, false) == 0, "a click on nothing sends nothing");
        info.entity2d = sprite->GetID();
        check(LayoutSendClick(context, info, false) == sprite->GetID(), "OnClick goes to the 2D entity");
        info.entity3d = entity->GetID();
        check(LayoutSendClick(context, info, true) == entity->GetID(), "OnDblClick goes to the 3D entity first");
        // End to end: a Wait Message waiting for OnClick in a Script of the
        // entity gets the click while the composition plays. The editor's
        // commands make the Script: Start linked to Wait Message.
        CKGUID waitGuid;
        for (int i = 0; i < CKGetPrototypeDeclarationCount(); ++i)
            if (CKObjectDeclaration *declaration = CKGetPrototypeDeclaration(i))
                if (declaration->GetName() && std::string(declaration->GetName()) == "Wait Message")
                    waitGuid = declaration->GetGuid();
        check(scene && waitGuid.IsValid(), "a scene and the Wait Message prototype");
        const EditResult created = scene && waitGuid.IsValid() ? CreateScript(document, entity->GetID(), "Click Script")
                                                               : EditResult::Refused("not attempted");
        const EditResult inserted =
            created.ok ? InsertBuildingBlock(document, created.created, created.created, waitGuid, Point(300, 100))
                       : EditResult::Refused("not attempted");
        CKBehavior *script = CKBehavior::Cast(context->GetObject(created.created));
        CKBehavior *wait = CKBehavior::Cast(context->GetObject(inserted.created));
        const bool made = script && wait &&
                          CreateBehaviorLink(document, created.created, StartEndpoint(document, created.created),
                                             LinkEndpoint(inserted.created, 0, ENDPOINT_BIN))
                              .ok;
        check(made, "the click Script is made");
        if (made) {
            // Its Message input says OnClick.
            const CKMessageType clickType = context->GetMessageManager()->AddMessageType((CKSTRING) "OnClick");
            for (int i = 0; i < wait->GetInputParameterCount(); ++i)
                if (wait->GetInputParameter(i)->GetGUID() == CKPGUID_MESSAGE)
                    if (CKParameter *source = wait->GetInputParameter(i)->GetDirectSource()) source->SetValue(&clickType);
            // Behaviors run for the objects the behavior manager holds: the
            // scene launched activates them by their scene flags.
            const CK_SCENEOBJECT_FLAGS active = CK_SCENEOBJECT_FLAGS(CK_SCENEOBJECT_START_ACTIVATE | CK_SCENEOBJECT_ACTIVE);
            scene->SetObjectFlags(entity, active);
            scene->SetObjectFlags(script, active);
            context->GetCurrentLevel()->LaunchScene(scene);
            scene->Activate(script, TRUE);
            context->Play();
            // Wait Message registers its wait with the message manager, which
            // activates its output when the message comes.
            auto outputActive = [&] {
                bool active = false;
                for (int frame = 0; frame < 3; ++frame) {
                    context->Process();
                    active = active || (wait->GetOutputCount() > 0 && wait->GetOutput(0)->IsActive());
                }
                return active;
            };
            const bool before = outputActive();
            info.entity2d = 0;
            LayoutSendClick(context, info, false);
            const bool after = outputActive();
            context->Pause();
            std::cout << "layout: Wait Message " << (before ? "out before the click" : "waits") << ", "
                      << (after ? "out after the click" : "still waits after the click") << '\n';
            check(!before && after, "Wait Message OnClick gets the 3D Layout's click");
            entity->RemoveScript(script->GetID());
            context->DestroyObject(script);
        }
        context->DestroyObject(entity);
        context->DestroyObject(sprite);
    }
    {
        // Add Parameter Operation (Dev.exe sub_5977D0): CK finds the operations
        // of the parts given, the inputs swapped too; Addition takes two Floats.
        const CKGUID floatType = CKPGUID_FLOAT, intType = CKPGUID_INT;
        const std::vector<OperationSignature> floats =
            AvailableOperationSignatures(context, nullptr, &floatType, &floatType, &floatType);
        const char *addition = nullptr;
        for (const OperationSignature &signature : floats)
            if (const char *name = context->GetParameterManager()->OperationGuidToName(signature.operation))
                if (std::string(name) == "Addition") addition = name;
        const std::vector<OperationSignature> any = AvailableOperationSignatures(context, nullptr, nullptr, nullptr, nullptr);
        const std::vector<OperationSignature> mixed =
            AvailableOperationSignatures(context, nullptr, &floatType, &intType, &floatType);
        std::cout << "layout: operations Float(Float, Float) " << floats.size() << ", any " << any.size()
                  << ", Float(Int, Float) " << mixed.size() << '\n';
        check(addition && any.size() > floats.size(), "Addition is among the Float(Float, Float) operations");
    }
    {
        // Player Mode's fullscreen device and mode (Dev.exe sub_4B2450); the
        // display is not switched here.
        CKRenderManager *renderer = context->GetRenderManager();
        check(LayoutFullScreenDriver(context, "") == 0 && LayoutFullScreenDriver(context, "no such driver") == 0,
              "an unknown Fullscreen Render Device is the first driver");
        const int last = renderer->GetRenderDriverCount() - 1;
        VxDriverDesc *desc = renderer->GetRenderDriverDescription(last);
        check(desc && LayoutFullScreenDriver(context, DriverName(desc)) == last,
              "the Fullscreen Render Device is found by name");
        VxDisplayMode found, wanted;
        bool listed = false;
        for (int i = 0; desc && i < DisplayModeCount(desc) && !listed; ++i)
            if (DisplayModeAt(desc, i).Bpp >= 16) {
                wanted = DisplayModeAt(desc, i);
                listed = true;
            }
        if (listed) {
            const std::string size = std::to_string(wanted.Width) + "x" + std::to_string(wanted.Height) + "x" +
                                     std::to_string(wanted.Bpp);
            check(LayoutFullScreenMode(context, last, size + "x" + std::to_string(wanted.RefreshRate), found) &&
                      found.Width == wanted.Width && found.Height == wanted.Height && found.Bpp == wanted.Bpp &&
                      found.RefreshRate == wanted.RefreshRate,
                  "a Render Mode of the driver is found");
            check(LayoutFullScreenMode(context, last, size + "x0", found) && found.Width == wanted.Width,
                  "a refresh rate of 0 takes any");
            check(LayoutFullScreenMode(context, last, size, found), "the refresh rate may be left out");
        }
        check(!LayoutFullScreenMode(context, last, "640x480x8x60", found) &&
                  !LayoutFullScreenMode(context, last, "-1x-1x-1", found) &&
                  !LayoutFullScreenMode(context, last, "123457x1x32x0", found) &&
                  !LayoutFullScreenMode(context, last + 1, "640x480x32x0", found),
              "no fullscreen for a mode the driver lacks or under 16 bits");
        std::cout << "layout: fullscreen driver " << last << " has " << (desc ? DisplayModeCount(desc) : 0)
                  << " modes\n";
        check(!view.FullScreen() && !view.RenderFullScreen(), "the view is windowed");
    }
    view.CompositionChanged();
    document.Close();
    check(view.Started(), "the render context survives closing");
    view.Stop();
    if (failures) return 1;
    std::cout << "Layout passed\n";
    return 0;
}

// The 3D Layout without a render context (no render device needed): the
// gestures' arithmetic (GestureMath); the view's cameras and icons made by
// CompositionChanged alone; a constant speed Zoom moving by the time a clock
// gives; Translate and Scale moving an entity, undone; a light's icon placed,
// rectangle-picked and picked with no frame drawn.
void LayoutGestures(CkRuntimeHost &host) {
    auto close = [](float a, float b, float tolerance = 0.001f) { return std::fabs(a - b) <= tolerance; };
    auto same = [&](const VxVector &a, const VxVector &b, float tolerance = 0.001f) {
        return close(a.x, b.x, tolerance) && close(a.y, b.y, tolerance) && close(a.z, b.z, tolerance);
    };
    const VxVector world[3] = {VxVector(1.0f, 0.0f, 0.0f), VxVector(0.0f, 1.0f, 0.0f), VxVector(0.0f, 0.0f, 1.0f)};

    // The arithmetic. Translate's plane: X and Y hold Z's normal; X alone,
    // seen from the front (looking along +Z), takes Y (seen longer than Z).
    VxMatrix front;
    Vx3DMatrixIdentity(front);
    CHECK(same(TranslatePlaneNormal(3, world, &front), VxVector(0.0f, 0.0f, 1.0f)));
    CHECK(same(TranslatePlaneNormal(1, world, &front), VxVector(0.0f, 0.0f, 1.0f)));
    CHECK(same(TranslatePlaneNormal(0, world, &front), VxVector(0.0f, 0.0f, 1.0f))); // the view's plane
    VxVector delta;
    CHECK(StickyTranslate(VxVector(0.0f, 0.0f, 1.0f), VxVector(0.0f, 0.0f, 10.0f), VxVector(2.0f, 3.0f, 0.0f),
                          VxVector(0.0f, 0.0f, 1.0f), delta) && same(delta, VxVector(2.0f, 3.0f, 0.0f)));
    CHECK(!StickyTranslate(VxVector(0.0f, 0.0f, 1.0f), VxVector(0.0f, 0.0f, -10.0f), VxVector(0.0f, 0.0f, 0.0f),
                           VxVector(0.0f, 0.0f, 1.0f), delta)); // behind the camera
    CHECK(same(ConstrainedMove(1, VxVector(1.0f, 2.0f, 3.0f), world), VxVector(1.0f, 0.0f, 0.0f)));
    CHECK(same(SlidingTranslate(6, 0.5f, 0.25f), VxVector(0.0f, 0.25f, 0.5f)));
    float first = 0.0f, second = 0.0f;
    RotateAngles(3, 0.5f, 0.25f, 180.0f, first, second);
    CHECK(close(first, 1.5707963f) && close(second, 0.7853981f));
    CHECK(close(ScaleStep(0.1f, 0.0f, 5.0f), 1.5f) && close(ScaleStep(0.0f, 0.1f, 5.0f), 0.5f));
    const VxVector volume = ScaleFactors(1, 3, 2.0f);
    CHECK(same(volume, VxVector(2.0f, 2.0f, 0.25f)) && close(volume.x * volume.y * volume.z, 1.0f));
    CHECK(same(ScaleFactors(2, 4, 2.0f), VxVector(1.0f, 1.0f, 2.0f)));
    CHECK(same(SnapCorrection(VxVector(1.0f, 0.0f, 0.0f), VxVector(1.0f, 5.0f, 0.0f), VxVector(1.5f, 7.0f, 0.0f)),
               VxVector(1.5f, 0.0f, 0.0f))); // only the axis moving
    const VxVector face[3] = {VxVector(0.0f, 0.0f, 0.0f), VxVector(10.0f, 0.0f, 0.0f), VxVector(0.0f, 10.0f, 0.0f)};
    VxVector target;
    CHECK(SnapToVertex(face, face, VxVector(9.0f, 1.0f, 0.0f), 3.0f, target) && same(target, face[1]));
    CHECK(!SnapToVertex(face, face, VxVector(5.0f, 5.0f, 0.0f), 3.0f, target));
    CHECK(SnapToEdge(face, face, VxVector(4.0f, 1.0f, 0.0f), 3.0f, target) && same(target, VxVector(4.0f, 0.0f, 0.0f)));
    float fov = 0.0f;
    CHECK(NextFov(1.0f, 0.5f, 0.0f, 1.0f, fov) && close(fov, 1.5f) && !NextFov(2.9f, 0.5f, 0.0f, 1.0f, fov));
    CHECK(close(NextOrthoZoom(1.0f, 2.0f, 0.5f), 2.0f) && NextOrthoZoom(1.0f, 2.0f, -1.0f) > 0.0f);
    const VxVector orbited = Orbited(VxVector(0.0f, 0.0f, 0.0f), world[0], 0.0f, 1.5707963f, VxVector(0.0f, 0.0f, 1.0f));
    // A quarter turn about Y: still on the XZ plane, as long, square to where it was.
    CHECK(close(Magnitude(orbited), 1.0f) && close(orbited.y, 0.0f) && close(orbited.z, 0.0f) && close(std::fabs(orbited.x), 1.0f));
    CHECK(same(Orbited(VxVector(1.0f, 2.0f, 3.0f), world[0], 0.0f, 0.0f, VxVector(0.0f, 0.0f, 1.0f)), VxVector(1.0f, 2.0f, 4.0f)));
    int offset[2] = {0, 0};
    bool found[2] = {false, false};
    SnapEdges(VxRect(0.0f, 0.0f, 10.0f, 10.0f), {VxRect(13.0f, 0.0f, 20.0f, 10.0f)}, 5, offset, found);
    CHECK(found[0] && offset[0] == 3 && !found[1]); // the right edge on the target's left one
    const Vx2DVector scaled = ScaleFactors2D(3, 10.0f, -5.0f, Vx2DVector(20.0f, 10.0f), 0.0f);
    CHECK(close(scaled.x, 1.5f) && close(scaled.y, 0.5f));
    CHECK(close(ScaleFactors2D(1, 3.0f, 0.0f, Vx2DVector(20.0f, 10.0f), 10.0f).x, 1.2f)); // 1.15 to 10 %

    // The view without a render context: its cameras and icons, 4:3 in 640 x 480.
    CKContext *context = host.Context();
    Document document(host); // a new composition (ClearAll), before the view makes its objects
    std::string error;
    CHECK(document.New(&error));
    document.History().SetLimit(100);
    CKLevel *level = context->GetCurrentLevel();
    CHECK(level);
    LayoutView view(host);
    view.CompositionChanged();
    CHECK(!view.Started() && view.InterfaceAlive());
    CHECK(view.Camera() && view.Camera()->GetID() == view.ViewCamera(LayoutViewCamera::Perspective));
    CHECK(!view.IconNames().empty());
    view.SetViewSize(640, 480);
    const VxRect rect = view.ViewRect();
    CHECK(close(rect.left, 0.0f) && close(rect.right, 640.0f) && close(rect.top, 0.0f) && close(rect.bottom, 480.0f));
    view.SetViewSize(800, 400);
    CHECK(close(view.ViewRect().GetWidth(), 533.0f, 1.0f) && close(view.ViewRect().left, 133.0f, 1.0f));
    view.SetViewSize(640, 480);

    // A constant speed Zoom: nothing while the mouse moves, then Step x the
    // seconds x the travel over the view's height each time.
    CKCamera *camera = view.Camera();
    VxVector start, moved;
    camera->GetPosition(&start);
    float now = 1000.0f;
    CameraGesture zoom;
    zoom.SetClock([&now] { return now; });
    CameraSteps steps;
    CHECK(steps.zoomConstant);
    CHECK(zoom.Begin(view, CameraAction::Zoom, steps, 320, 240, {}, false, false, false));
    zoom.Move(320, 192, false); // 48 pixels up: 0.1 of the view
    camera->GetPosition(&moved);
    CHECK(same(moved, start));
    now += 500.0f;
    zoom.Idle(false);
    camera->GetPosition(&moved);
    CHECK(close(Magnitude(moved - start), steps.zoom * 0.5f * 0.1f, 0.01f));
    now += 100.0f;
    zoom.Idle(false);
    camera->GetPosition(&moved);
    CHECK(close(Magnitude(moved - start), steps.zoom * 0.6f * 0.1f, 0.01f));
    zoom.Cancel();
    camera->GetPosition(&moved);
    CHECK(same(moved, start));

    // Translate along X out of Sticky Mode: the travel over the view times
    // the step; one undo step. Scale keeping the volume.
    auto *entity = CK3dEntity::Cast(context->CreateObject(CKCID_3DENTITY, (CKSTRING) "Gesture Entity"));
    CHECK(entity);
    level->AddObject(entity);
    const CK_ID id = entity->GetID();
    TransformSettings settings;
    settings.axes = 1;
    TransformSteps transformSteps;
    transformSteps.translateSticky = false;
    transformSteps.translate = 10.0f;
    TransformGesture translate;
    CHECK(translate.Begin(view, TransformAction::Translate, settings, transformSteps, 0, {id}, 320, 240, nullptr));
    translate.Move(384, 200); // 0.1 of the view's width along X; Y is not constrained
    VxVector position;
    entity->GetPosition(&position);
    CHECK(same(position, VxVector(1.0f, 0.0f, 0.0f)));
    CHECK(translate.End(&document));
    CHECK(document.History().Undo());
    entity->GetPosition(&position);
    CHECK(same(position, VxVector(0.0f, 0.0f, 0.0f)));
    TransformGesture scale;
    CHECK(scale.Begin(view, TransformAction::Scale, settings, transformSteps, 1, {id}, 320, 240, nullptr));
    scale.Move(384, 240); // 1 + 0.1 x 5
    VxVector size;
    entity->GetScale(&size);
    CHECK(same(size, VxVector(1.5f, 1.0f / 1.5f, 1.0f / 1.5f)));
    scale.Cancel();
    entity->GetScale(&size);
    CHECK(same(size, VxVector(1.0f, 1.0f, 1.0f)));
    context->DestroyObject(entity);

    // A light at the origin, in the middle of the view: its icon placed,
    // in a rectangle pick and picked, no frame drawn.
    auto *light = CKLight::Cast(context->CreateObject(CKCID_LIGHT, (CKSTRING) "Gesture Light"));
    CHECK(light);
    level->AddObject(light);
    ViewProjection projection;
    CHECK(view.Projection(projection));
    // Its icon around the origin (the bulb above it), in the view.
    bool placed = false;
    Vx2DVector middle;
    for (const LayoutView::IconHit &icon : view.PlaceIcons(projection, view.Camera()->GetID()))
        if (icon.owner == light->GetID()) {
            placed = icon.rect.left < 320.0f && icon.rect.right > 320.0f && icon.rect.top < 240.0f &&
                     icon.rect.bottom > 230.0f && icon.rect.GetWidth() > 1.0f;
            middle = icon.rect.GetCenter();
        }
    CHECK(placed);
    const std::vector<CK_ID> inRect = view.RectPick(VxRect(0.0f, 0.0f, 640.0f, 480.0f), true);
    CHECK(std::find(inRect.begin(), inRect.end(), light->GetID()) != inRect.end());
    CHECK(view.Pick(static_cast<int>(middle.x), static_cast<int>(middle.y)) == light->GetID());
    CHECK(view.Pick(20, 20) == 0);
    std::cout << "layout without a render context: " << view.IconNames().size() << " icon meshes, the light's icon picked\n";
    context->DestroyObject(light);
}

// The 3D Layout's projection without a render context (ViewProjection): the
// view's pixels from world points and back, a camera's own settings, the
// scale keeping an icon its size.
void ViewProjections(CkRuntimeHost &host) {
    auto close = [](float a, float b, float tolerance = 0.001f) { return std::fabs(a - b) <= tolerance; };
    ViewProjection projection; // at the origin, looking along +z
    Vx3DMatrixIdentity(projection.world);
    projection.view = VxRect(100.0f, 50.0f, 500.0f, 350.0f); // 400 x 300
    projection.fov = 1.0f;
    projection.front = 1.0f;
    projection.back = 1000.0f;
    projection.orthoZoom = 0.02f;
    // Straight ahead is the view's center.
    const VxVector center = projection.Project(VxVector(0.0f, 0.0f, 10.0f));
    CHECK(close(center.x, 300.0f) && close(center.y, 200.0f) && center.z > 0.0f && center.z < 1.0f);
    // A pixel's ray comes back to the pixel, deeper further along it.
    for (bool perspective : {true, false}) {
        projection.perspective = perspective;
        for (const VxVector pixel : {VxVector(120.0f, 60.0f, 0.0f), VxVector(300.0f, 200.0f, 0.0f), VxVector(480.0f, 340.0f, 0.0f)}) {
            VxVector origin, direction;
            CHECK(projection.Ray(pixel.x, pixel.y, origin, direction));
            const VxVector nearer = projection.Project(origin + direction * 2.0f);
            const VxVector further = projection.Project(origin + direction * 50.0f);
            CHECK(close(nearer.x, pixel.x) && close(nearer.y, pixel.y));
            CHECK(close(further.x, pixel.x) && close(further.y, pixel.y));
            CHECK(nearer.z > 0.0f && further.z > nearer.z && further.z < 1.0f);
        }
    }
    // Behind the camera there is no pixel.
    projection.perspective = true;
    CHECK(projection.Project(VxVector(0.0f, 0.0f, -5.0f)).z <= 0.0f);
    // A camera's own settings.
    CKContext *context = host.Context();
    CKCamera *camera = CKCamera::Cast(context->CreateObject(CKCID_CAMERA, (CKSTRING) "Projection Camera"));
    CHECK(camera);
    camera->SetProjectionType(CK_ORTHOGRAPHICPROJECTION);
    camera->SetFov(0.8f);
    camera->SetFrontPlane(2.0f);
    camera->SetBackPlane(500.0f);
    camera->SetOrthographicZoom(0.5f);
    const VxVector at(1.0f, 2.0f, 3.0f);
    camera->SetPosition(&at);
    const ViewProjection of = ViewProjection::Of(camera, projection.view);
    CHECK(!of.perspective && close(of.fov, 0.8f) && close(of.front, 2.0f) && close(of.back, 500.0f) &&
          close(of.orthoZoom, 0.5f));
    CHECK(close(of.world[3][0], 1.0f) && close(of.world[3][1], 2.0f) && close(of.world[3][2], 3.0f));
    context->DestroyObject(camera);
    // An icon has its own size (Dev: (101 - zoom) * 2 * 18 / tan(fov / 2) / 70 units away).
    const float zoom = 50.0f, distance = (101.0f - zoom) * 2.0f * 18.0f / std::tan(0.5f) / 70.0f;
    CHECK(close(projection.ConstantScale(VxVector(0.0f, 0.0f, distance), zoom), 1.0f, 0.0005f));
    CHECK(close(projection.ConstantScale(VxVector(0.0f, 0.0f, 2.0f * distance), zoom), 2.0f, 0.001f));
    std::cout << "View projections passed\n";
}

// The 3D Layout's checks that need no render context (--layout-objects): the
// Snapshot Buffer, the Hierarchy Manager's tree, CK2UI's parameter UIs, the
// Setups' attribute values, on the sample.
int LayoutObjectsFile(CkRuntimeHost &host, const std::string &path, const std::string &outputDirectory) {
    namespace fs = std::filesystem;
    std::cout << std::unitbuf; // lines reach the log before a runtime crash
    fs::create_directories(outputDirectory);
    CKContext *context = host.Context();
    int failures = 0;
    auto check = [&](bool condition, const std::string &what) {
        if (!condition) {
            std::cerr << "layout objects: " << what << '\n';
            ++failures;
        }
    };
    std::string error;
    Document document(host);
    UndoHistory &undo = document.History();
    undo.SetLimit(1000); // deep enough to count the steps
    if (!document.Open(path, &error)) {
        std::cerr << "open: " << error << '\n';
        return 2;
    }
    // The Snapshot Buffer (Dev.exe sub_4C5860, sub_4C5B20, sub_4C5C00).
    {
        SnapshotBuffer buffer;
        const size_t size = static_cast<size_t>(SnapshotBuffer::WIDTH) * SnapshotBuffer::HEIGHT * 4;
        check(buffer.FirstFree() == 0 && buffer.Put(std::vector<unsigned char>(size, 7)) == 0, "the first snapshot goes first");
        check(buffer.Put(std::vector<unsigned char>(size, 8), 0) == 1, "a full slot gives the next empty one");
        check(buffer.Put(std::vector<unsigned char>(size, 9), 5) == 5, "an empty slot asked for is used");
        buffer.Remove(0);
        check(!buffer.Has(0) && buffer.FirstFree() == 0, "deleting empties the slot");
        for (int i = 0; i < SnapshotBuffer::COUNT; ++i) buffer.Put(std::vector<unsigned char>(size, 1));
        check(buffer.FirstFree() == -1 && buffer.Put(std::vector<unsigned char>(size, 1)) == -1, "no more room for snapshots");
        // A 4 x 2 image halved: each pixel the average of its 2 x 2 block.
        const unsigned char image[4 * 2 * 4] = {0, 0, 0, 255, 100, 100, 100, 255, 10, 20, 30, 255, 30, 40, 50, 255,
                                                 200, 200, 200, 255, 100, 100, 100, 255, 50, 60, 70, 255, 70, 80, 90, 255};
        const std::vector<unsigned char> half = ShrinkImage(image, 4, 2, 16, 2, 1);
        check(half.size() == 8 && half[0] == 100 && half[4] == 40 && half[5] == 50 && half[6] == 60 && half[7] == 255,
              "Take averages the area of each pixel");
        // Capture: 2 x 2 blocks from the point; outside the view is white.
        std::vector<unsigned char> frame(16 * 16 * 4, 0);
        for (size_t i = 0; i < frame.size(); i += 4) frame[i + 2] = 200; // red
        const std::vector<unsigned char> captured = CaptureArea(frame.data(), 16, 16, 16 * 4, 2, 2);
        check(captured.size() == size && captured[2] == 200 && captured[0] == 0, "Capture averages 2 x 2 blocks");
        const size_t outside = (static_cast<size_t>(10) * SnapshotBuffer::WIDTH + 10) * 4;
        check(captured[outside] == 255 && captured[outside + 1] == 255 && captured[outside + 2] == 255,
              "Capture beyond the view is white");
    }
    // CK2UI.dll's parameter UIs (Managers\CK2UI.dll sub_24E52360).
    {
        CKParameterManager *manager = context->GetParameterManager();
        auto ui = [&](const CKGUID &guid) { return ParameterUIOf(manager, manager->ParameterGuidToType(guid)); };
        check(ui(CKPGUID_FLOAT).ui == ParameterUI::Number && ui(CKPGUID_FLOAT).dialog == 2001, "a Float's UI is one edit");
        const ParameterUIInfo vector = ui(CKPGUID_VECTOR);
        check(vector.ui == ParameterUI::Vector && vector.dialog == 2004 && vector.pick == 3 && vector.pickClass == CKCID_3DENTITY,
              "a Vector's UI is X, Y, Z picking 3D entities");
        check(ui(CKPGUID_SCRIPT).ui == ParameterUI::Script && ui(CKPGUID_SCRIPT).height == 28, "a Script's UI");
        const ParameterUIInfo entity = ui(CKPGUID_3DENTITY);
        check(entity.ui == ParameterUI::Object && entity.pickClass == CKCID_3DENTITY && entity.expand, "an object's UI");
        check(ui(CKPGUID_BOOL).ui == ParameterUI::Bool && ui(CKPGUID_BOOL).pick == 0, "a Boolean's check box");
        int enums = 0, flags = 0, structs = 0;
        for (int type = 0; type < manager->GetParameterTypesCount(); ++type) {
            CKParameterTypeDesc *description = manager->GetParameterTypeDescription(type);
            if (!description) continue;
            const ParameterUI kind = ParameterUIOf(manager, type).ui;
            if (description->dwFlags & CKPARAMETERTYPE_ENUMS) enums += kind == ParameterUI::Enum ? 1 : -1000;
            if (description->dwFlags & CKPARAMETERTYPE_FLAGS) flags += kind == ParameterUI::Flags ? 1 : -1000;
            if (description->dwFlags & CKPARAMETERTYPE_STRUCT) structs += kind == ParameterUI::Struct ? 1 : -1000;
        }
        check(enums > 0 && flags > 0 && structs > 0, "enumerations, flags and structures have their UIs");
        // The combos' lists: sorted, NULL with them, no private object.
        const ObjectChoices entities = ObjectsOfClass(document, CKCID_3DENTITY, true, "");
        bool sorted = true, null = false, hidden = false;
        for (size_t i = 0; i < entities.size(); ++i) {
            if (i && _stricmp(entities[i - 1].second.c_str(), entities[i].second.c_str()) > 0) sorted = false;
            if (!entities[i].first) null = entities[i].second == "NULL";
            else if (context->GetObject(entities[i].first)->IsPrivate()) hidden = true;
        }
        check(entities.size() > 1 && sorted && null && !hidden, "an Object UI lists the class's objects by name and NULL");
        const ObjectChoices owners = ScriptOwners(document, CKCID_BEOBJECT);
        bool scripted = owners.size() > 1;
        for (const auto &owner : owners)
            if (owner.first) {
                CKBeObject *object = CKBeObject::Cast(context->GetObject(owner.first));
                scripted = scripted && object && object->GetScriptCount() > 0;
                const ObjectChoices scripts = ScriptsOf(document, owner.first);
                check(!scripts.empty() && scripts.front().first, "a Script UI lists the owner's Scripts");
                break;
            }
        check(scripted, "a Script UI lists the objects with Scripts");
    }

    // Dev's Edit Parameter type (Dev.exe sub_589D50): a local takes another
    // type in one undo step.
    {
        CKParameterLocal *local = nullptr;
        for (const ScriptInfo &script : document.Scripts()) {
            CKBehavior *behavior = CKBehavior::Cast(context->GetObject(script.id));
            for (int i = 0; behavior && !local && i < behavior->GetLocalParameterCount(); ++i)
                if (CKParameterLocal *candidate = behavior->GetLocalParameter(i))
                    if (!candidate->IsMyselfParameter() && !(candidate->GetGUID() == CKPGUID_INT)) local = candidate;
            if (local) break;
        }
        if (local) {
            const CKGUID before = local->GetGUID();
            const EditResult typed = SetParameterType(document, local->GetID(), CKPGUID_INT);
            check(typed.ok && local->GetGUID() == CKPGUID_INT, "Edit Parameter changes a local's type");
            check(undo.Undo() && local->GetGUID() == before, "undo gives the type back");
            check(undo.Redo() && local->GetGUID() == CKPGUID_INT, "redo changes it again");
            undo.Undo();
        }
        // An input with its value holder: both take the type (the holder
        // used by this input only).
        auto readers = [&](CKParameterLocal *local) {
            int count = 0;
            const int inputs = context->GetObjectsCountByClassID(CKCID_PARAMETERIN);
            const CK_ID *ids = context->GetObjectsListByClassID(CKCID_PARAMETERIN);
            for (int i = 0; i < inputs; ++i)
                if (CKParameterIn *each = CKParameterIn::Cast(context->GetObject(ids[i])))
                    if (each->GetDirectSource() == local) ++count;
            return count;
        };
        CKParameterIn *input = nullptr;
        const XObjectPointerArray &behaviors = context->GetObjectListByType(CKCID_BEHAVIOR, TRUE);
        for (int i = 0; i < behaviors.Size() && !input; ++i)
            if (CKBehavior *behavior = CKBehavior::Cast(behaviors[i]))
                for (int k = 0; k < behavior->GetInputParameterCount() && !input; ++k)
                    if (CKParameterIn *candidate = behavior->GetInputParameter(k))
                        if (EditableValueOf(candidate) && readers(EditableValueOf(candidate)) == 1 &&
                            !(candidate->GetGUID() == CKPGUID_STRING))
                            input = candidate;
        if (input) {
            CKParameterLocal *holder = EditableValueOf(input);
            const CKGUID before = input->GetGUID(), held = holder->GetGUID();
            const EditResult typed = SetParameterType(document, input->GetID(), CKPGUID_STRING);
            check(typed.ok && input->GetGUID() == CKPGUID_STRING && holder->GetGUID() == CKPGUID_STRING,
                  "an input's type change reaches its value holder");
            check(undo.Undo() && input->GetGUID() == before && holder->GetGUID() == held,
                  "undo gives the input and its holder their types back");
        }
        // A holder another input reads too keeps its type (documentation
        // "Behavior Building Block": "if this one is linked only once").
        if (input) {
            CKParameterLocal *holder = EditableValueOf(input);
            CKParameterIn *other = nullptr;
            for (int i = 0; i < behaviors.Size() && !other; ++i)
                if (CKBehavior *behavior = CKBehavior::Cast(behaviors[i]))
                    for (int k = 0; k < behavior->GetInputParameterCount() && !other; ++k)
                        if (CKParameterIn *candidate = behavior->GetInputParameter(k))
                            if (candidate != input && !candidate->GetSharedSource() && EditableValueOf(candidate) != holder &&
                                context->GetParameterManager()->IsTypeCompatible(holder->GetType(), candidate->GetType()))
                                other = candidate;
            if (other) {
                // Borrowed: it reads the holder for the test, its own source given back after.
                CKParameter *own = other->GetDirectSource();
                other->SetDirectSource(holder);
                check(other->GetDirectSource() == holder, "a second input reads the holder");
                const CKGUID held = holder->GetGUID();
                const EditResult typed = SetParameterType(document, input->GetID(), CKPGUID_STRING);
                check(typed.ok && input->GetGUID() == CKPGUID_STRING && holder->GetGUID() == held,
                      "a value holder read by two inputs keeps its type");
                undo.Undo();
                other->SetDirectSource(own);
            }
        }
    }

    // CK2UI's Dependencies, Object Array and LOD Options UIs.
    {
        CKParameterManager *manager = context->GetParameterManager();
        const CKParameterType copyType = manager->ParameterGuidToType(CKPGUID_COPYDEPENDENCIES);
        check(DependenciesModeOf(manager, copyType) == CK_DEPENDENCIES_COPY &&
                  DependenciesModeOf(manager, manager->ParameterGuidToType(CKPGUID_DELETEDEPENDENCIES)) == CK_DEPENDENCIES_DELETE &&
                  DependenciesModeOf(manager, manager->ParameterGuidToType(CKPGUID_SAVEDEPENDENCIES)) == CK_DEPENDENCIES_SAVE &&
                  DependenciesModeOf(manager, manager->ParameterGuidToType(CKPGUID_REPLACEDEPENDENCIES)) == CK_DEPENDENCIES_REPLACE &&
                  DependenciesModeOf(manager, manager->ParameterGuidToType(CKPGUID_FLOAT)) == 0,
              "the dependencies types' operations");
        check(ParameterUIOf(manager, copyType).ui == ParameterUI::Dependencies &&
                  ParameterUIOf(manager, manager->ParameterGuidToType(CKPGUID_OBJECTARRAY)).ui == ParameterUI::ObjectArray,
              "the Dependencies and Object Array UIs");
        const CKParameterType lod = manager->ParameterGuidToType(CKGUID(0x2b2d2f43, 0x02ab7bc5));
        if (lod >= 0) check(ParameterUIOf(manager, lod).ui == ParameterUI::LodOptions, "the LOD Options UI");
        std::cout << "layout: LOD Options type " << (lod >= 0 ? "registered" : "not registered") << '\n';
        // The tree: depth-first, each level one deeper at most, brothers by
        // name, every class with options in the mode.
        const std::vector<DependencyClass> tree = DependencyClasses(CK_DEPENDENCIES_COPY);
        bool shaped = !tree.empty() && tree.front().depth == 0, complete = true;
        for (size_t i = 1; i < tree.size(); ++i) {
            if (tree[i].depth > tree[i - 1].depth + 1) shaped = false;
            // The previous brother at the same depth sorts before.
            for (size_t k = i; k-- > 0;) {
                if (tree[k].depth < tree[i].depth) break;
                if (tree[k].depth == tree[i].depth) {
                    if (_stricmp(tree[k].name.c_str(), tree[i].name.c_str()) > 0) shaped = false;
                    break;
                }
            }
        }
        for (CK_CLASSID cid = 0; cid < static_cast<CK_CLASSID>(CKGetClassCount()); ++cid) {
            CKClassDesc *description = CKGetClassDesc(cid);
            if (!description || !description->DependsCountFct || !description->DependsCountFct(CK_DEPENDENCIES_COPY)) continue;
            if (!CKIsChildClassOf(cid, CKCID_OBJECT)) continue;
            bool listed = false;
            for (const DependencyClass &entry : tree) listed = listed || entry.cid == cid;
            complete = complete && listed;
        }
        check(shaped && complete, "the Dependencies tree lists the classes with options under their ancestors, by name");
        const std::vector<std::string> options = DependencyOptions(CKCID_3DENTITY, CK_DEPENDENCIES_COPY);
        check(!options.empty() && !options.front().empty(), "a 3D entity's copy options");
        // The box's values: edited in the editor's memory, set by the
        // types' copy functions (CK's heap), each parameter's own.
        CKParameterOut *dependencies = context->CreateCKParameterOut((CKSTRING) "deps", CKPGUID_COPYDEPENDENCIES, TRUE);
        CKParameterOut *copy = context->CreateCKParameterOut((CKSTRING) "deps copy", CKPGUID_COPYDEPENDENCIES, TRUE);
        CKDependencies edited;
        edited.m_Flags = CK_DEPENDENCIES_CUSTOM;
        edited.Resize(CKGetClassCount());
        for (int i = 0; i < edited.Size(); ++i) edited[i] = 0;
        edited[CKCID_3DENTITY] = 5;
        CKDependencies *held = nullptr, *copied = nullptr;
        check(SetHeldValue(dependencies, &edited), "a dependencies value is set");
        dependencies->GetValue(&held, FALSE);
        check(held && held != &edited && held->m_Flags == CK_DEPENDENCIES_CUSTOM && held->Size() > CKCID_3DENTITY &&
                  (*held)[CKCID_3DENTITY] == 5,
              "the parameter holds a copy of it");
        copy->CopyValue(dependencies, FALSE);
        copy->GetValue(&copied, FALSE);
        check(copied && copied != held && copied->Size() > CKCID_3DENTITY && (*copied)[CKCID_3DENTITY] == 5,
              "a dependencies value is copied");
        edited[CKCID_3DENTITY] = 1;
        SetHeldValue(copy, &edited);
        check((*held)[CKCID_3DENTITY] == 5 && (*copied)[CKCID_3DENTITY] == 1, "a copied dependencies value is its own");
        CKParameterOut *objects = context->CreateCKParameterOut((CKSTRING) "objects", CKPGUID_OBJECTARRAY, TRUE);
        CKParameterOut *objectsCopy = context->CreateCKParameterOut((CKSTRING) "objects copy", CKPGUID_OBJECTARRAY, TRUE);
        XObjectArray list;
        const ObjectChoices entities = ObjectsOfClass(document, CKCID_3DENTITY, true, "");
        for (const auto &entity : entities)
            if (entity.first) list.PushBack(entity.first);
        check(SetHeldValue(objects, &list), "an object array is set");
        objectsCopy->CopyValue(objects, FALSE);
        XObjectArray *first = nullptr, *second = nullptr;
        objects->GetValue(&first, FALSE);
        objectsCopy->GetValue(&second, FALSE);
        check(first && second && first != second && second->Size() == list.Size() && list.Size() > 0 && (*second)[0] == list[0],
              "an object array is copied");
        list.RemoveAt(0);
        SetHeldValue(objectsCopy, &list);
        check(first->Size() == second->Size() + 1, "a copied object array is its own");
        for (CKObject *made : {static_cast<CKObject *>(dependencies), static_cast<CKObject *>(copy),
                               static_cast<CKObject *>(objects), static_cast<CKObject *>(objectsCopy)})
            context->DestroyObject(made);
    }

    // CK2UI's Synchro UI makes the object typed in (sub_24E732D0).
    {
        const EditResult made = CreateNamedObject(document, CKCID_SYNCHRO, "Synchro UI Test");
        CKObject *synchro = context->GetObject(made.created);
        // (CKLevel::AddObject keeps scene objects only: the level does not list it.)
        check(made.ok && CKSynchroObject::Cast(synchro) && synchro->GetName() == std::string("Synchro UI Test"),
              "a synchro object is made");
        check(undo.Undo() && undo.Redo() && context->GetObject(made.created), "its creation is undone and redone");
        undo.Undo();
        check(!CreateNamedObject(document, CKCID_SYNCHRO, "").ok, "an object needs a name");
    }

    // The Setup's attribute values (Dev.exe sub_4436E0): the Edit Parameter
    // box writes an attribute's parameter as a value step (no Script).
    {
        CKAttributeManager *attributes = context->GetAttributeManager();
        CKParameterManager *manager = context->GetParameterManager();
        CKAttributeType floatAttribute = -1;
        for (CKAttributeType type = 0; attributes && type < attributes->GetAttributeCount() && floatAttribute < 0; ++type)
            if (attributes->IsAttributeIndexValid(type) &&
                manager->ParameterTypeToGuid(attributes->GetAttributeParameterType(type)) == CKPGUID_FLOAT)
                floatAttribute = type;
        const ObjectChoices objects = ObjectsOfClass(document, CKCID_3DOBJECT, true, "");
        CKBeObject *owner = nullptr;
        for (const auto &choice : objects)
            if (!owner && choice.first) owner = CKBeObject::Cast(context->GetObject(choice.first));
        if (floatAttribute >= 0 && owner) {
            const bool had = owner->HasAttribute(floatAttribute) != FALSE;
            if (!had) AddAttribute(document, {owner->GetID()}, floatAttribute);
            CKParameterOut *value = owner->GetAttributeParameter(floatAttribute);
            std::cout << "layout: attribute " << attributes->GetAttributeNameByType(floatAttribute) << " value type "
                      << (value ? manager->ParameterTypeToName(value->GetType()) : "none") << std::endl;
            if (value && !(value->GetGUID() == CKPGUID_FLOAT)) value = nullptr;
            float before = 0.0f;
            if (value) value->GetValue(&before, FALSE);
            const float after = before + 2.5f;
            // (Float is set as text, as the box sets the types with a copy function.)
            const EditResult set = value ? SetParameterText(document, 0, value->GetID(), std::to_string(after))
                                         : EditResult::Refused("not attempted");
            float now = 0.0f;
            if (value) value->GetValue(&now, FALSE);
            if (value) check(set.ok && now == after, "an attribute's value is set: " + set.message);
            if (value && set.ok)
                check(undo.Undo() && (value->GetValue(&now, FALSE), now == before), "undo gives the attribute its value back");
            if (!had) undo.Undo();
        }
    }
    const std::string saved = (fs::path(outputDirectory) / "layout.cmo").string();
    SaveOptions options;
    options.allowOverwrite = true;
    SaveReport report;
    check(SaveDocumentAs(document, saved, options, report), "save with the 3D Layout");
    for (const auto &message : report.errors) std::cerr << "  " << message << '\n';
    check(document.Open(saved, &error), "reopen: " + error);
    int interfaceObjects = 0;
    for (CK_CLASSID cid : {CKCID_CAMERA, CKCID_3DENTITY, CKCID_SPRITETEXT, CKCID_MESH, CKCID_MATERIAL}) {
        const XObjectPointerArray &objects = context->GetObjectListByType(cid, TRUE);
        for (int i = 0; i < objects.Size(); ++i) {
            const char *name = objects[i]->GetName();
            if (name && (std::strstr(name, "Virtools_") == name || std::strstr(name, " View"))) ++interfaceObjects;
        }
    }
    check(interfaceObjects == 0, "no 3D Layout object is saved");
    check(!context->GetObjectByNameAndClass((CKSTRING) "Selection Group Test", CKCID_GROUP, nullptr),
          "selection groups are not saved");
    std::cout << "layout: reopened level render contexts " << context->GetCurrentLevel()->GetRenderContextCount()
              << '\n';
    {
        // Delete Options (Dev.exe sub_5BF7B0): None keeps an entity's mesh,
        // Full takes it with the entity.
        std::vector<CK3dEntity *> meshed, objects;
        const XObjectPointerArray &list = context->GetObjectListByType(CKCID_3DOBJECT, TRUE);
        for (int i = 0; i < list.Size(); ++i)
            if (CK3dEntity *entity = CK3dEntity::Cast(list[i])) objects.push_back(entity);
        for (CK3dEntity *entity : objects)
            if (entity->GetCurrentMesh() && entity->GetMeshCount() == 1 && !entity->IsPrivate()) meshed.push_back(entity);
        if (meshed.size() >= 2) {
            for (int i = 0; i < 2; ++i) {
                CKDependencies dependencies = *CKGetDefaultClassDependencies(CK_DEPENDENCIES_DELETE);
                dependencies.m_Flags = i == 0 ? CK_DEPENDENCIES_NONE : CK_DEPENDENCIES_FULL;
                const CK_ID mesh = meshed[i]->GetCurrentMesh()->GetID();
                // A mesh another entity uses would stay anyway.
                bool sharedMesh = false;
                for (CK3dEntity *other : objects)
                    if (other != meshed[i] && context->GetObject(other->GetID()) == other && other->GetCurrentMesh() &&
                        other->GetCurrentMesh()->GetID() == mesh)
                        sharedMesh = true;
                const EditResult deleted = DestroyLevelObjects(document, {meshed[i]->GetID()}, &dependencies);
                const bool kept = context->GetObject(mesh) != nullptr;
                std::cout << "layout: delete options " << (i == 0 ? "none" : "full") << (kept ? " keeps" : " deletes")
                          << " the mesh\n";
                check(deleted.ok && (sharedMesh || kept == (i == 0)), "the delete options choose whether the mesh goes");
            }
        }
    }
    document.Close();
    if (failures) return 1;
    std::cout << "Layout objects passed\n";
    return 0;
}

} // namespace vdevtest
