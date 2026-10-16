#include "core/level/ObjectProperties.h"
#include "core/runtime/SdkCompat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace vdev {

namespace {

CKCamera *Camera(CKObject *object) { return CKCamera::Cast(object); }
CKLight *Light(CKObject *object) { return CKLight::Cast(object); }
CKMaterial *Material(CKObject *object) { return CKMaterial::Cast(object); }

// Another value: a step away, back when it would leave the range.
float Away(float value, float step, float limit) { return value + step <= limit ? value + step : value - step; }
float Away(float value, float step) { return value + step; }
VxColor AwayColor(VxColor color) {
    auto channel = [](float c) { return c < 0.5f ? c + 0.4f : c - 0.4f; };
    return VxColor(channel(color.r), channel(color.g), channel(color.b), color.a);
}
// The next of first..last.
int Next(int value, int first, int last) { return value >= first && value < last ? value + 1 : first; }

VxColor KeepAlpha(const VxColor &rgb, const VxColor &current) { return VxColor(rgb.r, rgb.g, rgb.b, current.a); }

CK3dEntity *Entity3D(CKObject *object) { return CK3dEntity::Cast(object); }
CK2dEntity *Entity2D(CKObject *object) { return CK2dEntity::Cast(object); }
CKSprite *Sprite(CKObject *object) { return CKSprite::Cast(object); }
CKTexture *Texture(CKObject *object) { return CKTexture::Cast(object); }
CKMesh *Mesh(CKObject *object) { return CKMesh::Cast(object); }
CKGrid *Grid(CKObject *object) { return CKGrid::Cast(object); }
CKCurve *Curve(CKObject *object) { return CKCurve::Cast(object); }
CKCurvePoint *CurvePoint(CKObject *object) { return CKCurvePoint::Cast(object); }
CKSound *Sound(CKObject *object) { return CKSound::Cast(object); }
CKWaveSound *WaveSound(CKObject *object) { return CKWaveSound::Cast(object); }
CKAnimation *Animation(CKObject *object) { return CKAnimation::Cast(object); }
CKObjectAnimation *ObjectAnimation(CKObject *object) { return CKObjectAnimation::Cast(object); }
CKSpriteText *SpriteText(CKObject *object) { return CKSpriteText::Cast(object); }

// An ARGB color (CK's CKDWORD): with its alpha, or opaque.
VxColor ColorOf(CKDWORD argb, bool alpha) {
    return VxColor(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f,
                   alpha ? ((argb >> 24) & 0xFF) / 255.0f : 1.0f);
}

// A curve point's curve follows it.
void UpdatePoint(CKObject *object) {
    CKCurvePoint *point = CurvePoint(object);
    point->NotifyUpdate();
    if (CKCurve *curve = point->GetCurve()) curve->Update();
}

template <CKDWORD Bit> bool AnimationFlag(CKObject *object) { return (Animation(object)->GetFlags() & Bit) != 0; }
template <CKDWORD Bit> void SetAnimationFlag(CKObject *object, bool on) {
    CKAnimation *animation = Animation(object);
    animation->SetFlags(on ? animation->GetFlags() | Bit : animation->GetFlags() & ~Bit);
}
template <CKDWORD Bit> bool TransitionBit(CKObject *object) {
    return (static_cast<CKDWORD>(Animation(object)->GetTransitionMode()) & Bit) != 0;
}
template <CKDWORD Bit> void SetTransitionBit(CKObject *object, bool on) {
    const CKDWORD mode = static_cast<CKDWORD>(Animation(object)->GetTransitionMode());
    Animation(object)->SetTransitionMode(static_cast<CK_ANIMATION_TRANSITION_MODE>(on ? mode | Bit : mode & ~Bit));
}
const CKDWORD SECONDARY_PLAY = CKSECONDARYANIMATION_ONESHOT | CKSECONDARYANIMATION_LOOP | CKSECONDARYANIMATION_LASTFRAME |
                               CKSECONDARYANIMATION_LOOPNTIMES;

// Other values for the checks.
bool Not(bool value) { return !value; }
int Plus1(int value) { return value + 1; }
int Plus10(int value) { return value + 10; }
int OneMinus(int value) { return value ? 0 : 1; }
float Plus10f(float value) { return value + 10.0f; }
float AwayUnit(float value) { return Away(value, 0.3f, 1.0f); }
float AwayPitch(float value) { return Away(value, 0.3f, 2.0f); }
int Away255(int value) { return value < 128 ? value + 80 : value - 80; }
int AwayIterations(int value) { return value < 10 ? value + 1 : value - 1; }
int OtherSlot(int value) { return value == 0 ? 1 : 0; }
int NextPixelFormat(int value) { return Next(value, _32_ARGB8888, _16_ARGB4444); }
int NextBitmapSave(int value) { return Next(value, CKTEXTURE_RAWDATA, CKTEXTURE_INCLUDEORIGINALFILE); }
int NextSoundSave(int value) { return Next(value, CKSOUND_EXTERNAL, CKSOUND_USEGLOBAL); }
int NextBlend(int value) { return Next(value, VXTEXTUREBLEND_DECAL, VXTEXTUREBLEND_MAX); }
int NextFilter(int value) { return Next(value, VXTEXTUREFILTER_NEAREST, VXTEXTUREFILTER_LINEARMIPLINEAR); }
int NextAddress(int value) { return Next(value, VXTEXTURE_ADDRESSWRAP, VXTEXTURE_ADDRESSCLAMP); }
int NextWrap(int value) { return Next(value, VXTEXTUREWRAP_NONE, VXTEXTUREWRAP_UV); }
int NextOrientation(int value) { return Next(value, 0, 3); }
int NextSpriteMode(int value) { return Next(value, VXSPRITE3D_BILLBOARD, VXSPRITE3D_ORIENTABLE); }
int OtherSoundType(int value) { return value == CK_WAVESOUND_POINT ? CK_WAVESOUND_BACKGROUND : CK_WAVESOUND_POINT; }
int OtherSecondaryPlay(int value) {
    return value == CKSECONDARYANIMATION_ONESHOT ? CKSECONDARYANIMATION_LOOP : CKSECONDARYANIMATION_ONESHOT;
}
int OtherAlignment(int value) {
    const int topLeft = CKSPRITETEXT_LEFT | CKSPRITETEXT_TOP;
    return value == topLeft ? CKSPRITETEXT_RIGHT | CKSPRITETEXT_BOTTOM : topLeft;
}
VxVector AwayCone(VxVector cone) {
    return VxVector(cone.x < 180.0f ? cone.x + 30.0f : cone.x - 30.0f, cone.y < 180.0f ? cone.y + 40.0f : cone.y - 40.0f,
                    Away(cone.z, 0.3f, 1.0f));
}
Vx2DVector AwayDistance(Vx2DVector range) { return Vx2DVector(range.x + 1.0f, range.y + 20.0f); }
Vx2DVector AwayOffset(Vx2DVector offset) { return Vx2DVector(offset.x + 0.25f, offset.y - 0.25f); }
std::string OtherText(std::string text) { return text == "Property" ? "Other" : "Property"; }

} // namespace

namespace properties {

const ObjectProperty<int> CameraProjection = {
    "Projection Type", CKCID_CAMERA, [](CKObject *o) { return static_cast<int>(Camera(o)->GetProjectionType()); },
    [](CKObject *o, int v) { Camera(o)->SetProjectionType(v); },
    [](int v) { return v == CK_PERSPECTIVEPROJECTION ? CK_ORTHOGRAPHICPROJECTION : CK_PERSPECTIVEPROJECTION; }};
const ObjectProperty<bool> CameraAspectRatioActive = {
    "Aspect Ratio Active", CKCID_CAMERA,
    [](CKObject *o) { return !(CK3dEntity::Cast(o)->GetFlags() & CK_3DENTITY_CAMERAIGNOREASPECT); },
    [](CKObject *o, bool v) {
        CK3dEntity *e = CK3dEntity::Cast(o);
        e->SetFlags(v ? e->GetFlags() & ~CK_3DENTITY_CAMERAIGNOREASPECT : e->GetFlags() | CK_3DENTITY_CAMERAIGNOREASPECT);
    },
    [](bool v) { return !v; }};
const ObjectProperty<float> CameraOrthoZoom = {
    "Ortho Zoom", CKCID_CAMERA, [](CKObject *o) { return Camera(o)->GetOrthographicZoom(); },
    [](CKObject *o, float v) { Camera(o)->SetOrthographicZoom(v); }, [](float v) { return Away(v, 0.1f, 0.5f); }};
const ObjectProperty<float> CameraFov = {
    "Field Of View", CKCID_CAMERA, [](CKObject *o) { return Camera(o)->GetFov(); },
    [](CKObject *o, float v) { Camera(o)->SetFov(v); }, [](float v) { return Away(v, 0.3f, 3.0f); }};
const ObjectProperty<float> CameraNearClip = {
    "Near Clip", CKCID_CAMERA, [](CKObject *o) { return Camera(o)->GetFrontPlane(); },
    [](CKObject *o, float v) { Camera(o)->SetFrontPlane(v); }, [](float v) { return Away(v, 1.0f); }};
const ObjectProperty<float> CameraFarClip = {
    "Far Clip", CKCID_CAMERA, [](CKObject *o) { return Camera(o)->GetBackPlane(); },
    [](CKObject *o, float v) { Camera(o)->SetBackPlane(v); }, [](float v) { return Away(v, 100.0f); }};

const ObjectProperty<bool> LightSpecular = {
    "Specular", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetSpecularFlag() != 0; },
    [](CKObject *o, bool v) { Light(o)->SetSpecularFlag(v); }, [](bool v) { return !v; }};
const ObjectProperty<int> LightType = {
    "Light Type", CKCID_LIGHT, [](CKObject *o) { return static_cast<int>(Light(o)->GetType()); },
    [](CKObject *o, int v) { Light(o)->SetType(static_cast<VXLIGHT_TYPE>(v)); },
    [](int v) { return Next(v, VX_LIGHTPOINT, VX_LIGHTDIREC); }};
const ObjectProperty<bool> LightActive = {
    "Light Active", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetActivity() != 0; },
    [](CKObject *o, bool v) { Light(o)->Active(v); }, [](bool v) { return !v; }};
const ObjectProperty<VxColor> LightColor = {
    "Light Color", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetColor(); },
    [](CKObject *o, VxColor v) { Light(o)->SetColor(v); }, AwayColor};
const ObjectProperty<float> LightRange = {
    "Light Range", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetRange(); },
    [](CKObject *o, float v) { Light(o)->SetRange(v); }, [](float v) { return Away(v, 10.0f); }};
const ObjectProperty<float> LightPower = {
    "Light Power", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetLightPower(); },
    [](CKObject *o, float v) { Light(o)->SetLightPower(v); }, [](float v) { return Away(v, 1.0f, 10.0f); }};
const ObjectProperty<float> LightHotSpot = {
    "Hot Spot", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetHotSpot(); },
    [](CKObject *o, float v) { Light(o)->SetHotSpot(v); }, [](float v) { return Away(v, 0.2f, 3.0f); }};
const ObjectProperty<float> LightFallOff = {
    "Fall Off", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetFallOff(); },
    [](CKObject *o, float v) { Light(o)->SetFallOff(v); }, [](float v) { return Away(v, 0.2f, 3.0f); }};
const ObjectProperty<float> LightFallOffShape = {
    "Fall Off Shape", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetFallOffShape(); },
    [](CKObject *o, float v) { Light(o)->SetFallOffShape(v); }, [](float v) { return Away(v, 0.5f, 10.0f); }};
const ObjectProperty<float> LightConstantAttenuation = {
    "Constant Attenuation", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetConstantAttenuation(); },
    [](CKObject *o, float v) { Light(o)->SetConstantAttenuation(v); }, [](float v) { return Away(v, 0.5f, 10.0f); }};
const ObjectProperty<float> LightLinearAttenuation = {
    "Linear Attenuation", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetLinearAttenuation(); },
    [](CKObject *o, float v) { Light(o)->SetLinearAttenuation(v); }, [](float v) { return Away(v, 0.5f, 10.0f); }};
const ObjectProperty<float> LightQuadraticAttenuation = {
    "Quadratic Attenuation", CKCID_LIGHT, [](CKObject *o) { return Light(o)->GetQuadraticAttenuation(); },
    [](CKObject *o, float v) { Light(o)->SetQuadraticAttenuation(v); }, [](float v) { return Away(v, 0.5f, 10.0f); }};

const ObjectProperty<VxColor> MaterialAmbient = {
    "Ambient", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->GetAmbient(); },
    [](CKObject *o, VxColor v) { Material(o)->SetAmbient(KeepAlpha(v, Material(o)->GetAmbient())); }, AwayColor};
const ObjectProperty<VxColor> MaterialDiffuse = {
    "Diffuse", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->GetDiffuse(); },
    [](CKObject *o, VxColor v) { Material(o)->SetDiffuse(KeepAlpha(v, Material(o)->GetDiffuse())); }, AwayColor};
const ObjectProperty<VxColor> MaterialSpecular = {
    "Specular", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->GetSpecular(); },
    [](CKObject *o, VxColor v) { Material(o)->SetSpecular(KeepAlpha(v, Material(o)->GetSpecular())); }, AwayColor};
const ObjectProperty<VxColor> MaterialEmissive = {
    "Emissive", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->GetEmissive(); },
    [](CKObject *o, VxColor v) { Material(o)->SetEmissive(KeepAlpha(v, Material(o)->GetEmissive())); }, AwayColor};
const ObjectProperty<int> MaterialAlpha = {
    "Alpha", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetDiffuse().a * 255.0f + 0.5f); },
    [](CKObject *o, int v) {
        VxColor c = Material(o)->GetDiffuse();
        c.a = v / 255.0f;
        Material(o)->SetDiffuse(c);
    },
    [](int v) { return v < 128 ? v + 80 : v - 80; }};
const ObjectProperty<float> MaterialPower = {
    "Power", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->GetPower(); },
    [](CKObject *o, float v) { Material(o)->SetPower(v); }, [](float v) { return Away(v, 10.0f, 100.0f); }};
const ObjectProperty<int> MaterialMode = {
    "Material Mode", CKCID_MATERIAL,
    [](CKObject *o) {
        CKMaterial *m = Material(o);
        const bool blend = m->AlphaBlendEnabled() != FALSE, test = m->AlphaTestEnabled() != FALSE;
        const bool zwrite = m->ZWriteEnabled() != FALSE, lessEqual = m->GetZFunc() == VXCMP_LESSEQUAL;
        const bool alphaBlend = m->GetSourceBlend() == VXBLEND_SRCALPHA && m->GetDestBlend() == VXBLEND_INVSRCALPHA;
        if (!blend && m->GetSourceBlend() == VXBLEND_ONE && m->GetDestBlend() == VXBLEND_ZERO && zwrite && lessEqual && !test)
            return 0;
        if (blend && alphaBlend && !zwrite && lessEqual && !test) return 1;
        if (blend && alphaBlend && zwrite && lessEqual && test && m->GetAlphaFunc() == VXCMP_GREATEREQUAL) return 2;
        return 3;
    },
    [](CKObject *o, int v) {
        CKMaterial *m = Material(o);
        if (v < 0 || v > 2) return;
        m->EnableAlphaBlend(v != 0);
        m->EnableAlphaTest(v == 2);
        m->SetSourceBlend(v ? VXBLEND_SRCALPHA : VXBLEND_ONE);
        m->SetDestBlend(v ? VXBLEND_INVSRCALPHA : VXBLEND_ZERO);
        m->EnableZWrite(v != 1);
        m->SetZFunc(VXCMP_LESSEQUAL);
        if (v == 0) {
            // Opaque: the diffuse alpha back to 1.
            VxColor diffuse = m->GetDiffuse();
            diffuse.a = 1.0f;
            m->SetDiffuse(diffuse);
        } else if (v == 2) {
            m->SetAlphaFunc(VXCMP_GREATEREQUAL);
            m->SetAlphaRef(1);
        }
    },
    [](int v) { return v == 0 ? 1 : 0; }};
const ObjectProperty<int> MaterialSourceBlend = {
    "Source Blend", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetSourceBlend()); },
    [](CKObject *o, int v) { Material(o)->SetSourceBlend(static_cast<VXBLEND_MODE>(v)); },
    [](int v) { return Next(v, VXBLEND_ZERO, VXBLEND_BOTHINVSRCALPHA); }};
const ObjectProperty<int> MaterialDestBlend = {
    "Dest Blend", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetDestBlend()); },
    [](CKObject *o, int v) { Material(o)->SetDestBlend(static_cast<VXBLEND_MODE>(v)); },
    [](int v) { return Next(v, VXBLEND_ZERO, VXBLEND_BOTHINVSRCALPHA); }};
const ObjectProperty<bool> MaterialZWrite = {
    "Z Write", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->ZWriteEnabled() != 0; },
    [](CKObject *o, bool v) { Material(o)->EnableZWrite(v); }, [](bool v) { return !v; }};
const ObjectProperty<bool> MaterialAlphaBlend = {
    "Blend", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->AlphaBlendEnabled() != 0; },
    [](CKObject *o, bool v) { Material(o)->EnableAlphaBlend(v); }, [](bool v) { return !v; }};
const ObjectProperty<int> MaterialZFunc = {
    "Z Compare Function", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetZFunc()); },
    [](CKObject *o, int v) { Material(o)->SetZFunc(static_cast<VXCMPFUNC>(v)); },
    [](int v) { return Next(v, VXCMP_NEVER, VXCMP_ALWAYS); }};
const ObjectProperty<bool> MaterialAlphaTest = {
    "Alpha Test", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->AlphaTestEnabled() != 0; },
    [](CKObject *o, bool v) { Material(o)->EnableAlphaTest(v); }, [](bool v) { return !v; }};
const ObjectProperty<int> MaterialAlphaFunc = {
    "Alpha Test Function", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetAlphaFunc()); },
    [](CKObject *o, int v) { Material(o)->SetAlphaFunc(static_cast<VXCMPFUNC>(v)); },
    [](int v) { return Next(v, VXCMP_NEVER, VXCMP_ALWAYS); }};
const ObjectProperty<int> MaterialAlphaRef = {
    "Alpha Ref Value", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetAlphaRef()); },
    [](CKObject *o, int v) { Material(o)->SetAlphaRef(static_cast<CKBYTE>(std::max(0, std::min(255, v)))); },
    [](int v) { return v < 128 ? v + 80 : v - 80; }};
const ObjectProperty<bool> MaterialBothSided = {
    "Both Sided", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->IsTwoSided() != 0; },
    [](CKObject *o, bool v) { Material(o)->SetTwoSided(v); }, [](bool v) { return !v; }};
const ObjectProperty<int> MaterialFillMode = {
    "Fill Mode", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetFillMode()); },
    [](CKObject *o, int v) { Material(o)->SetFillMode(static_cast<VXFILL_MODE>(v)); },
    [](int v) { return Next(v, VXFILL_POINT, VXFILL_SOLID); }};
const ObjectProperty<int> MaterialShadeMode = {
    "Shade Mode", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetShadeMode()); },
    [](CKObject *o, int v) { Material(o)->SetShadeMode(static_cast<VXSHADE_MODE>(v)); },
    [](int v) { return Next(v, VXSHADE_FLAT, VXSHADE_PHONG); }, false};
const ObjectProperty<int> MaterialEffect = {
    "Effect", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetEffect()); },
    [](CKObject *o, int v) { Material(o)->SetEffect(static_cast<VX_EFFECT>(v)); },
    [](int v) { return v == 0 ? 1 : 0; }, false};

const ObjectProperty<bool> Entity3DPickable = {"Pickable", CKCID_3DENTITY, [](CKObject *o) { return Entity3D(o)->IsPickable() != 0; }, [](CKObject *o, bool v) { Entity3D(o)->SetPickable(v); }, Not};
const ObjectProperty<bool> Entity2DPickable = {"Pickable", CKCID_2DENTITY, [](CKObject *o) { return Entity2D(o)->IsPickable() != 0; }, [](CKObject *o, bool v) { Entity2D(o)->SetPickable(v); }, Not};
const ObjectProperty<bool> Entity2DHomogeneous = {"Homogeneous Coordinates", CKCID_2DENTITY, [](CKObject *o) { return Entity2D(o)->IsHomogeneousCoordinates() != 0; }, [](CKObject *o, bool v) { Entity2D(o)->SetHomogeneousCoordinates(v); }, Not};
const ObjectProperty<bool> Entity2DClipToCamera = {"Clip To Camera", CKCID_2DENTITY, [](CKObject *o) { return Entity2D(o)->IsClippedToCamera() != 0; }, [](CKObject *o, bool v) { Entity2D(o)->EnableClipToCamera(v); }, Not};
const ObjectProperty<bool> Entity2DRatioOffset = {"Relative To Viewport", CKCID_2DENTITY, [](CKObject *o) { return Entity2D(o)->IsRatioOffset() != 0; }, [](CKObject *o, bool v) { Entity2D(o)->EnableRatioOffset(v); }, Not};
const ObjectProperty<int> Entity2DZOrder = {"Z Order", CKCID_2DENTITY, [](CKObject *o) { return Entity2D(o)->GetZOrder(); }, [](CKObject *o, int v) { Entity2D(o)->SetZOrder(v); }, Plus1};
const ObjectProperty<bool> Entity2DBackground = {"Background Sprite", CKCID_2DENTITY, [](CKObject *o) { return Entity2D(o)->IsBackground() != 0; }, [](CKObject *o, bool v) { Entity2D(o)->SetBackground(v); }, Not};
const ObjectProperty<bool> Entity2DSourceRect = {"Active", CKCID_2DENTITY, [](CKObject *o) { return Entity2D(o)->IsUsingSourceRect() != 0; }, [](CKObject *o, bool v) { Entity2D(o)->UseSourceRect(v); }, Not};
const ObjectProperty<bool> Entity2DClipToParent = {"Clip to Parent", CKCID_2DENTITY, [](CKObject *o) { return Entity2D(o)->IsClipToParent() != 0; }, [](CKObject *o, bool v) { Entity2D(o)->SetClipToParent(v); }, Not};
const ObjectProperty<bool> Entity2DStickLeft = {"Stick Left", CKCID_2DENTITY, [](CKObject *o) { return (Entity2D(o)->GetFlags() & CK_2DENTITY_STICKLEFT) != 0; }, [](CKObject *o, bool v) { Entity2D(o)->ModifyFlags(v ? CK_2DENTITY_STICKLEFT : 0, v ? 0 : CK_2DENTITY_STICKLEFT); }, Not};
const ObjectProperty<bool> Entity2DStickRight = {"Stick Right", CKCID_2DENTITY, [](CKObject *o) { return (Entity2D(o)->GetFlags() & CK_2DENTITY_STICKRIGHT) != 0; }, [](CKObject *o, bool v) { Entity2D(o)->ModifyFlags(v ? CK_2DENTITY_STICKRIGHT : 0, v ? 0 : CK_2DENTITY_STICKRIGHT); }, Not};
const ObjectProperty<bool> Entity2DStickTop = {"Stick Top", CKCID_2DENTITY, [](CKObject *o) { return (Entity2D(o)->GetFlags() & CK_2DENTITY_STICKTOP) != 0; }, [](CKObject *o, bool v) { Entity2D(o)->ModifyFlags(v ? CK_2DENTITY_STICKTOP : 0, v ? 0 : CK_2DENTITY_STICKTOP); }, Not};
const ObjectProperty<bool> Entity2DStickBottom = {"Stick Bottom", CKCID_2DENTITY, [](CKObject *o) { return (Entity2D(o)->GetFlags() & CK_2DENTITY_STICKBOTTOM) != 0; }, [](CKObject *o, bool v) { Entity2D(o)->ModifyFlags(v ? CK_2DENTITY_STICKBOTTOM : 0, v ? 0 : CK_2DENTITY_STICKBOTTOM); }, Not};
const ObjectProperty<int> SpriteVideoFormat = {"Desired Video Format", CKCID_SPRITE, [](CKObject *o) { return static_cast<int>(Sprite(o)->GetDesiredVideoFormat()); }, [](CKObject *o, int v) { Sprite(o)->SetDesiredVideoFormat(static_cast<VX_PIXELFORMAT>(v)); }, NextPixelFormat, false};
const ObjectProperty<int> SpriteCurrentSlot = {"Current Slot", CKCID_SPRITE, [](CKObject *o) { return Sprite(o)->GetCurrentSlot(); }, [](CKObject *o, int v) { Sprite(o)->SetCurrentSlot(v); }, OtherSlot};
const ObjectProperty<int> SpriteSaveOptions = {"Save Options", CKCID_SPRITE, [](CKObject *o) { return static_cast<int>(Sprite(o)->GetSaveOptions()); }, [](CKObject *o, int v) { Sprite(o)->SetSaveOptions(static_cast<CK_BITMAP_SAVEOPTIONS>(v)); }, NextBitmapSave, false};
const ObjectProperty<bool> SpriteTransparent = {"Color Key Transparency", CKCID_SPRITE, [](CKObject *o) { return Sprite(o)->IsTransparent() != 0; }, [](CKObject *o, bool v) { Sprite(o)->SetTransparent(v); }, Not};
const ObjectProperty<VxColor> SpriteTransparentColor = {"Transparent Color", CKCID_SPRITE, [](CKObject *o) { return ColorOf(Sprite(o)->GetTransparentColor(), false); }, [](CKObject *o, VxColor v) { Sprite(o)->SetTransparentColor(RGBAFTOCOLOR(v.r, v.g, v.b, 1.0f)); }, AwayColor};
const ObjectProperty<int> TextureVideoFormat = {"Desired Video Format", CKCID_TEXTURE, [](CKObject *o) { return static_cast<int>(Texture(o)->GetDesiredVideoFormat()); }, [](CKObject *o, int v) { Texture(o)->SetDesiredVideoFormat(static_cast<VX_PIXELFORMAT>(v)); }, NextPixelFormat};
const ObjectProperty<int> TextureCurrentSlot = {"Current Slot", CKCID_TEXTURE, [](CKObject *o) { return Texture(o)->GetCurrentSlot(); }, [](CKObject *o, int v) { Texture(o)->SetCurrentSlot(v); }, OtherSlot};
const ObjectProperty<int> TextureSaveOptions = {"Save Options", CKCID_TEXTURE, [](CKObject *o) { return static_cast<int>(Texture(o)->GetSaveOptions()); }, [](CKObject *o, int v) { Texture(o)->SetSaveOptions(static_cast<CK_BITMAP_SAVEOPTIONS>(v)); }, NextBitmapSave};
const ObjectProperty<bool> TextureTransparent = {"Color Key Transparency", CKCID_TEXTURE, [](CKObject *o) { return Texture(o)->IsTransparent() != 0; }, [](CKObject *o, bool v) { Texture(o)->SetTransparent(v); }, Not};
const ObjectProperty<VxColor> TextureTransparentColor = {"Transparent Color", CKCID_TEXTURE, [](CKObject *o) { return ColorOf(Texture(o)->GetTransparentColor(), false); }, [](CKObject *o, VxColor v) { Texture(o)->SetTransparentColor(RGBAFTOCOLOR(v.r, v.g, v.b, 1.0f)); }, AwayColor};
const ObjectProperty<int> TextureMipmap = {"Mip Levels", CKCID_TEXTURE, [](CKObject *o) { return Texture(o)->GetMipmapCount() > 0 ? 1 : 0; }, [](CKObject *o, int v) { Texture(o)->UseMipmap(v); }, OneMinus, true, false};
const ObjectProperty<bool> TextureCubeMap = {"Cube Map", CKCID_TEXTURE, [](CKObject *o) { return Texture(o)->IsCubeMap() != 0; }, [](CKObject *o, bool v) { Texture(o)->SetCubeMap(v); }, Not};
const ObjectProperty<int> TexturePickThreshold = {"Alpha Picking Threshold", CKCID_TEXTURE, [](CKObject *o) { return Texture(o)->GetPickThreshold(); }, [](CKObject *o, int v) { Texture(o)->SetPickThreshold(v); }, Away255, false};
const ObjectProperty<int> MaterialTextureBlend = {"Texture Blend", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetTextureBlendMode()); }, [](CKObject *o, int v) { Material(o)->SetTextureBlendMode(static_cast<VXTEXTURE_BLENDMODE>(v)); }, NextBlend};
const ObjectProperty<int> MaterialTextureMinMode = {"Filter Min", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetTextureMinMode()); }, [](CKObject *o, int v) { Material(o)->SetTextureMinMode(static_cast<VXTEXTURE_FILTERMODE>(v)); }, NextFilter};
const ObjectProperty<int> MaterialTextureMagMode = {"Filter Mag", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetTextureMagMode()); }, [](CKObject *o, int v) { Material(o)->SetTextureMagMode(static_cast<VXTEXTURE_FILTERMODE>(v)); }, NextFilter};
const ObjectProperty<int> MaterialTextureAddress = {"Address Mode", CKCID_MATERIAL, [](CKObject *o) { return static_cast<int>(Material(o)->GetTextureAddressMode()); }, [](CKObject *o, int v) { Material(o)->SetTextureAddressMode(static_cast<VXTEXTURE_ADDRESSMODE>(v)); }, NextAddress};
const ObjectProperty<VxColor> MaterialTextureBorderColor = {"Border Color", CKCID_MATERIAL, [](CKObject *o) { return VxColor(Material(o)->GetTextureBorderColor()); }, [](CKObject *o, VxColor v) { Material(o)->SetTextureBorderColor(RGBAFTOCOLOR(&v)); }, AwayColor};
const ObjectProperty<bool> MaterialPerspectiveCorrect = {"Perspective Correct", CKCID_MATERIAL, [](CKObject *o) { return Material(o)->PerspectiveCorrectionEnabled() != 0; }, [](CKObject *o, bool v) { Material(o)->EnablePerspectiveCorrection(v); }, Not};
const ObjectProperty<bool> MeshProgressive = {"Progressive Mesh", CKCID_MESH, [](CKObject *o) { return Mesh(o)->IsPM() != 0; }, [](CKObject *o, bool v) { if (v) Mesh(o)->CreatePM(); else Mesh(o)->DestroyPM(); }, Not};
const ObjectProperty<int> MeshWrapMode = {"Texture Wrap", CKCID_MESH, [](CKObject *o) { return static_cast<int>(Mesh(o)->GetWrapMode()); }, [](CKObject *o, int v) { Mesh(o)->SetWrapMode(static_cast<VXTEXTURE_WRAPMODE>(v)); }, NextWrap};
const ObjectProperty<bool> MeshLit = {"Lit Mode", CKCID_MESH, [](CKObject *o) { return Mesh(o)->GetLitMode() == VX_LITMESH; }, [](CKObject *o, bool v) { Mesh(o)->SetLitMode(v ? VX_LITMESH : VX_PRELITMESH); }, Not};
const ObjectProperty<int> PatchMeshIterations = {"Iterations", CKCID_PATCHMESH, [](CKObject *o) { return CKPatchMesh::Cast(o)->GetIterationCount(); }, [](CKObject *o, int v) { CKPatchMesh::Cast(o)->SetIterationCount(v); CKPatchMesh::Cast(o)->BuildRenderMesh(); }, AwayIterations};
const ObjectProperty<int> GridOrientation = {"Orientation Mode", CKCID_GRID, [](CKObject *o) { return static_cast<int>(Grid(o)->GetOrientationMode()); }, [](CKObject *o, int v) { Grid(o)->SetOrientationMode(static_cast<CK_GRIDORIENTATION>(v)); }, NextOrientation};
const ObjectProperty<int> GridPriority = {"Grid Priority", CKCID_GRID, [](CKObject *o) { return Grid(o)->GetGridPriority(); }, [](CKObject *o, int v) { Grid(o)->SetGridPriority(v); }, Plus1};
const ObjectProperty<bool> LayerVisible = {"Layer Visibility", CKCID_LAYER, [](CKObject *o) { return static_cast<CKLayer *>(o)->IsVisible() != 0; }, [](CKObject *o, bool v) { static_cast<CKLayer *>(o)->SetVisible(v); }, Not};
const ObjectProperty<bool> CurveOpen = {"Open Curve", CKCID_CURVE, [](CKObject *o) { return Curve(o)->IsOpen() != 0; }, [](CKObject *o, bool v) { if (v) Curve(o)->Open(); else Curve(o)->Close(); }, Not};
const ObjectProperty<int> CurveStepCount = {"Step Count", CKCID_CURVE, [](CKObject *o) { return Curve(o)->GetStepCount(); }, [](CKObject *o, int v) { Curve(o)->SetStepCount(v); }, Plus10};
const ObjectProperty<float> CurveFitting = {"Fitting Coefficient", CKCID_CURVE, [](CKObject *o) { return Curve(o)->GetFittingCoeff(); }, [](CKObject *o, float v) { Curve(o)->SetFittingCoeff(v); Curve(o)->Update(); }, AwayUnit};
const ObjectProperty<float> CurvePointBias = {"Bias", CKCID_CURVEPOINT, [](CKObject *o) { return CurvePoint(o)->GetBias(); }, [](CKObject *o, float v) { CurvePoint(o)->SetBias(v); UpdatePoint(o); }, AwayUnit};
const ObjectProperty<float> CurvePointTension = {"Tension", CKCID_CURVEPOINT, [](CKObject *o) { return CurvePoint(o)->GetTension(); }, [](CKObject *o, float v) { CurvePoint(o)->SetTension(v); UpdatePoint(o); }, AwayUnit};
const ObjectProperty<float> CurvePointContinuity = {"Continuity", CKCID_CURVEPOINT, [](CKObject *o) { return CurvePoint(o)->GetContinuity(); }, [](CKObject *o, float v) { CurvePoint(o)->SetContinuity(v); UpdatePoint(o); }, AwayUnit};
const ObjectProperty<bool> CurvePointLinear = {"Next Segment Is Linear", CKCID_CURVEPOINT, [](CKObject *o) { return CurvePoint(o)->IsLinear() != 0; }, [](CKObject *o, bool v) { CurvePoint(o)->SetLinear(v); UpdatePoint(o); }, Not};
const ObjectProperty<int> SoundSaveOptions = {"Save Options", CKCID_SOUND, [](CKObject *o) { return static_cast<int>(Sound(o)->GetSaveOptions()); }, [](CKObject *o, int v) { Sound(o)->SetSaveOptions(static_cast<CK_SOUND_SAVEOPTIONS>(v)); }, NextSoundSave};
const ObjectProperty<int> WaveSoundType = {"Sound Type", CKCID_WAVESOUND, [](CKObject *o) { return static_cast<int>(WaveSound(o)->GetType()); }, [](CKObject *o, int v) { WaveSound(o)->SetType(static_cast<CK_WAVESOUND_TYPE>(v)); }, OtherSoundType, true, false};
const ObjectProperty<bool> WaveSoundLoop = {"Loop", CKCID_WAVESOUND, [](CKObject *o) { return WaveSound(o)->GetLoopMode() != 0; }, [](CKObject *o, bool v) { WaveSound(o)->SetLoopMode(v); }, Not};
const ObjectProperty<float> WaveSoundGain = {"Gain", CKCID_WAVESOUND, [](CKObject *o) { return WaveSound(o)->GetGain(); }, [](CKObject *o, float v) { WaveSound(o)->SetGain(v); }, AwayUnit};
const ObjectProperty<float> WaveSoundPitch = {"Pitch", CKCID_WAVESOUND, [](CKObject *o) { return WaveSound(o)->GetPitch(); }, [](CKObject *o, float v) { WaveSound(o)->SetPitch(v); }, AwayPitch};
const ObjectProperty<VxVector> WaveSoundCone = {"Sound Cone", CKCID_WAVESOUND, [](CKObject *o) { float inside = 0.0f, outside = 0.0f, gain = 0.0f; WaveSound(o)->GetCone(&inside, &outside, &gain); return VxVector(inside, outside, gain); }, [](CKObject *o, VxVector v) { WaveSound(o)->SetCone(v.x, v.y, v.z); }, AwayCone};
const ObjectProperty<Vx2DVector> WaveSoundDistance = {"Sound Distance", CKCID_WAVESOUND, [](CKObject *o) { float minimum = 0.0f, maximum = 0.0f; CKDWORD behavior = 1; WaveSound(o)->GetMinMaxDistance(&minimum, &maximum, &behavior); return Vx2DVector(minimum, maximum); }, [](CKObject *o, Vx2DVector v) { float minimum = 0.0f, maximum = 0.0f; CKDWORD behavior = 1; WaveSound(o)->GetMinMaxDistance(&minimum, &maximum, &behavior); WaveSound(o)->SetMinMaxDistance(v.x, std::max(v.x, v.y), behavior); }, AwayDistance};
const ObjectProperty<int> Sprite3DMode = {"Sprite Type", CKCID_SPRITE3D, [](CKObject *o) { return static_cast<int>(CKSprite3D::Cast(o)->GetMode()); }, [](CKObject *o, int v) { CKSprite3D::Cast(o)->SetMode(static_cast<VXSPRITE3D_TYPE>(v)); }, NextSpriteMode};
const ObjectProperty<Vx2DVector> Sprite3DOffset = {"Sprite Offset", CKCID_SPRITE3D, [](CKObject *o) { Vx2DVector offset; CKSprite3D::Cast(o)->GetOffset(offset); return offset; }, [](CKObject *o, Vx2DVector v) { Vx2DVector offset(v); CKSprite3D::Cast(o)->SetOffset(offset); }, AwayOffset};
const ObjectProperty<float> AnimationLength = {"Animation Length", CKCID_ANIMATION, [](CKObject *o) { return Animation(o)->GetLength(); }, [](CKObject *o, float v) { Animation(o)->SetLength(std::max(1.0f, v)); }, Plus10f};
const ObjectProperty<bool> AnimationLinked = {"Link To Frame Rate", CKCID_ANIMATION, [](CKObject *o) { return Animation(o)->IsLinkedToFrameRate() != 0; }, [](CKObject *o, bool v) { Animation(o)->LinkToFrameRate(v, Animation(o)->GetLinkedFrameRate()); }, Not};
const ObjectProperty<float> AnimationFrameRate = {"Frame Rate", CKCID_ANIMATION, [](CKObject *o) { return Animation(o)->GetLinkedFrameRate(); }, [](CKObject *o, float v) { Animation(o)->LinkToFrameRate(TRUE, std::max(1.0f, v)); }, Plus10f};
const ObjectProperty<float> AnimationMergeFactor = {"Merge Factor", CKCID_ANIMATION, [](CKObject *o) { return Animation(o)->GetMergeFactor(); }, [](CKObject *o, float v) { Animation(o)->SetMergeFactor(v); }, AwayUnit};
const ObjectProperty<bool> AnimationCanBeInterrupted = {"Can be interrupted", CKCID_ANIMATION, [](CKObject *o) { return AnimationFlag<CKANIMATION_CANBEBREAK>(o); }, [](CKObject *o, bool v) { SetAnimationFlag<CKANIMATION_CANBEBREAK>(o, v); }, Not};
const ObjectProperty<bool> AnimationAlignOrientation = {"Character takes its orientation", CKCID_ANIMATION, [](CKObject *o) { return AnimationFlag<CKANIMATION_ALIGNORIENTATION>(o); }, [](CKObject *o, bool v) { SetAnimationFlag<CKANIMATION_ALIGNORIENTATION>(o, v); }, Not};
const ObjectProperty<bool> AnimationTransitionPreset = {"Preset (ignore the modes asked for)", CKCID_ANIMATION, [](CKObject *o) { return AnimationFlag<CKANIMATION_TRANSITION_PRESET>(o); }, [](CKObject *o, bool v) { SetAnimationFlag<CKANIMATION_TRANSITION_PRESET>(o, v); }, Not};
const ObjectProperty<bool> AnimationSecondaryPreset = {"Preset##secondary", CKCID_ANIMATION, [](CKObject *o) { return AnimationFlag<CKANIMATION_SECONDARY_PRESET>(o); }, [](CKObject *o, bool v) { SetAnimationFlag<CKANIMATION_SECONDARY_PRESET>(o, v); }, Not};
const ObjectProperty<bool> AnimationTransitionFromNow = {"Transition Mode", CKCID_ANIMATION, [](CKObject *o) { return TransitionBit<CK_TRANSITION_FROMNOW>(o); }, [](CKObject *o, bool v) { SetTransitionBit<CK_TRANSITION_FROMNOW>(o, v); }, Not};
const ObjectProperty<bool> AnimationTransitionWarpFromCurrent = {"Transition Mode", CKCID_ANIMATION, [](CKObject *o) { return TransitionBit<CK_TRANSITION_FROMWARPFROMCURRENT>(o); }, [](CKObject *o, bool v) { SetTransitionBit<CK_TRANSITION_FROMWARPFROMCURRENT>(o, v); }, Not};
const ObjectProperty<bool> AnimationTransitionToStart = {"Transition Mode", CKCID_ANIMATION, [](CKObject *o) { return TransitionBit<CK_TRANSITION_TOSTART>(o); }, [](CKObject *o, bool v) { SetTransitionBit<CK_TRANSITION_TOSTART>(o, v); }, Not};
const ObjectProperty<bool> AnimationTransitionWarpToStart = {"Transition Mode", CKCID_ANIMATION, [](CKObject *o) { return TransitionBit<CK_TRANSITION_WARPTOSTART>(o); }, [](CKObject *o, bool v) { SetTransitionBit<CK_TRANSITION_WARPTOSTART>(o, v); }, Not};
const ObjectProperty<bool> AnimationTransitionWarpToBest = {"Transition Mode", CKCID_ANIMATION, [](CKObject *o) { return TransitionBit<CK_TRANSITION_WARPTOBEST>(o); }, [](CKObject *o, bool v) { SetTransitionBit<CK_TRANSITION_WARPTOBEST>(o, v); }, Not};
const ObjectProperty<bool> AnimationTransitionWarpToSamePosition = {"Transition Mode", CKCID_ANIMATION, [](CKObject *o) { return TransitionBit<CK_TRANSITION_WARPTOSAMEPOS>(o); }, [](CKObject *o, bool v) { SetTransitionBit<CK_TRANSITION_WARPTOSAMEPOS>(o, v); }, Not};
const ObjectProperty<bool> AnimationTransitionUseVelocity = {"Transition Mode", CKCID_ANIMATION, [](CKObject *o) { return TransitionBit<CK_TRANSITION_USEVELOCITY>(o); }, [](CKObject *o, bool v) { SetTransitionBit<CK_TRANSITION_USEVELOCITY>(o, v); }, Not};
const ObjectProperty<bool> AnimationTransitionLoopIfEqual = {"Transition Mode", CKCID_ANIMATION, [](CKObject *o) { return TransitionBit<CK_TRANSITION_LOOPIFEQUAL>(o); }, [](CKObject *o, bool v) { SetTransitionBit<CK_TRANSITION_LOOPIFEQUAL>(o, v); }, Not};
const ObjectProperty<int> AnimationSecondaryPlay = {"Secondary Animation Mode", CKCID_ANIMATION, [](CKObject *o) { return static_cast<int>(static_cast<CKDWORD>(Animation(o)->GetSecondaryAnimationMode()) & SECONDARY_PLAY); }, [](CKObject *o, int v) { const CKDWORD mode = static_cast<CKDWORD>(Animation(o)->GetSecondaryAnimationMode()); Animation(o)->SetSecondaryAnimationMode(static_cast<CK_SECONDARYANIMATION_FLAGS>((mode & ~SECONDARY_PLAY) | static_cast<CKDWORD>(v))); }, OtherSecondaryPlay};
const ObjectProperty<bool> AnimationSecondaryWarp = {"Secondary Animation Mode", CKCID_ANIMATION, [](CKObject *o) { return (static_cast<CKDWORD>(Animation(o)->GetSecondaryAnimationMode()) & CKSECONDARYANIMATION_DOWARP) != 0; }, [](CKObject *o, bool v) { const CKDWORD mode = static_cast<CKDWORD>(Animation(o)->GetSecondaryAnimationMode()); Animation(o)->SetSecondaryAnimationMode(static_cast<CK_SECONDARYANIMATION_FLAGS>(v ? mode | CKSECONDARYANIMATION_DOWARP : mode & ~CKSECONDARYANIMATION_DOWARP)); }, Not};
const ObjectProperty<float> ObjectAnimationLength = {"Animation Length", CKCID_OBJECTANIMATION, [](CKObject *o) { return ObjectAnimation(o)->GetLength(); }, [](CKObject *o, float v) { ObjectAnimation(o)->SetLength(std::max(1.0f, v)); }, Plus10f};
const ObjectProperty<float> ObjectAnimationMergeFactor = {"Merge Factor", CKCID_OBJECTANIMATION, [](CKObject *o) { return ObjectAnimation(o)->GetMergeFactor(); }, [](CKObject *o, float v) { ObjectAnimation(o)->SetMergeFactor(v); }, AwayUnit, false};
const ObjectProperty<bool> ObjectAnimationIgnorePosition = {"Ignore Animation Information", CKCID_OBJECTANIMATION, [](CKObject *o) { return (ObjectAnimation(o)->GetFlags() & CK_OBJECTANIMATION_IGNOREPOS) != 0; }, [](CKObject *o, bool v) { CKObjectAnimation *a = ObjectAnimation(o); a->SetFlags(v ? a->GetFlags() | CK_OBJECTANIMATION_IGNOREPOS : a->GetFlags() & ~CK_OBJECTANIMATION_IGNOREPOS); }, Not};
const ObjectProperty<bool> ObjectAnimationIgnoreRotation = {"Ignore Animation Information", CKCID_OBJECTANIMATION, [](CKObject *o) { return (ObjectAnimation(o)->GetFlags() & CK_OBJECTANIMATION_IGNOREROT) != 0; }, [](CKObject *o, bool v) { CKObjectAnimation *a = ObjectAnimation(o); a->SetFlags(v ? a->GetFlags() | CK_OBJECTANIMATION_IGNOREROT : a->GetFlags() & ~CK_OBJECTANIMATION_IGNOREROT); }, Not};
const ObjectProperty<bool> ObjectAnimationIgnoreScale = {"Ignore Animation Information", CKCID_OBJECTANIMATION, [](CKObject *o) { return (ObjectAnimation(o)->GetFlags() & CK_OBJECTANIMATION_IGNORESCALE) != 0; }, [](CKObject *o, bool v) { CKObjectAnimation *a = ObjectAnimation(o); a->SetFlags(v ? a->GetFlags() | CK_OBJECTANIMATION_IGNORESCALE : a->GetFlags() & ~CK_OBJECTANIMATION_IGNORESCALE); }, Not};
const ObjectProperty<bool> ObjectAnimationIgnoreOffAxisScale = {"Ignore Animation Information", CKCID_OBJECTANIMATION, [](CKObject *o) { return (ObjectAnimation(o)->GetFlags() & CK_OBJECTANIMATION_IGNORESCALEROT) != 0; }, [](CKObject *o, bool v) { CKObjectAnimation *a = ObjectAnimation(o); a->SetFlags(v ? a->GetFlags() | CK_OBJECTANIMATION_IGNORESCALEROT : a->GetFlags() & ~CK_OBJECTANIMATION_IGNORESCALEROT); }, Not};
const ObjectProperty<bool> ObjectAnimationIgnoreMorph = {"Ignore Animation Information", CKCID_OBJECTANIMATION, [](CKObject *o) { return (ObjectAnimation(o)->GetFlags() & CK_OBJECTANIMATION_IGNOREMORPH) != 0; }, [](CKObject *o, bool v) { CKObjectAnimation *a = ObjectAnimation(o); a->SetFlags(v ? a->GetFlags() | CK_OBJECTANIMATION_IGNOREMORPH : a->GetFlags() & ~CK_OBJECTANIMATION_IGNOREMORPH); }, Not};
const ObjectProperty<std::string> SpriteTextText = {"Sprite Text", CKCID_SPRITETEXT, [](CKObject *o) { return std::string(SpriteText(o)->GetText() ? SpriteText(o)->GetText() : ""); }, [](CKObject *o, std::string v) { SpriteText(o)->SetText(const_cast<CKSTRING>(v.c_str())); }, OtherText};
const ObjectProperty<VxColor> SpriteTextColor = {"Text Color", CKCID_SPRITETEXT, [](CKObject *o) { return ColorOf(SpriteText(o)->GetTextColor(), true); }, [](CKObject *o, VxColor v) { SpriteText(o)->SetTextColor(RGBAFTOCOLOR(v.r, v.g, v.b, v.a)); }, AwayColor};
const ObjectProperty<VxColor> SpriteTextBackgroundColor = {"Background Color", CKCID_SPRITETEXT, [](CKObject *o) { return ColorOf(SpriteText(o)->GetBackgroundTextColor(), true); }, [](CKObject *o, VxColor v) { SpriteText(o)->SetBackgroundColor(RGBAFTOCOLOR(v.r, v.g, v.b, v.a)); }, AwayColor};
const ObjectProperty<int> SpriteTextAlignment = {"Text Alignment", CKCID_SPRITETEXT, [](CKObject *o) { return static_cast<int>(SpriteTextAlign(SpriteText(o))); }, [](CKObject *o, int v) { SpriteText(o)->SetAlign(static_cast<CKSPRITETEXT_ALIGNMENT>(v)); }, OtherAlignment, false};
const ObjectProperty<int> SynchroUsers = {"Set Number Of Users", CKCID_SYNCHRO, [](CKObject *o) { return CKSynchroObject::Cast(o)->GetRendezVousNumberOfWaiters(); }, [](CKObject *o, int v) { CKSynchroObject::Cast(o)->SetRendezVousNumberOfWaiters(std::max(0, v)); }, Plus1};
const ObjectProperty<bool> StateActive = {"State Active", CKCID_STATE, [](CKObject *o) { return CKStateObject::Cast(o)->IsStateActive() != 0; }, [](CKObject *o, bool v) { if (v) CKStateObject::Cast(o)->EnterState(); else CKStateObject::Cast(o)->LeaveState(); }, Not};

} // namespace properties

namespace {

std::string Text(bool value) { return value ? "true" : "false"; }
std::string Text(int value) { return std::to_string(value); }
std::string Text(float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", value);
    return text;
}
std::string Text(const VxColor &value) {
    return Text(value.r) + " " + Text(value.g) + " " + Text(value.b) + " " + Text(value.a);
}
std::string Text(const VxVector &value) { return Text(value.x) + " " + Text(value.y) + " " + Text(value.z); }
std::string Text(const Vx2DVector &value) { return Text(value.x) + " " + Text(value.y); }
std::string Text(const std::string &value) { return "\"" + value + "\""; }

// The same value: the texts equal; colors to the 8 bits a state keeps of each channel.
template <typename T> bool SameText(const std::string &a, const std::string &b) { return a == b; }
template <> bool SameText<VxColor>(const std::string &a, const std::string &b) {
    std::istringstream x(a), y(b);
    for (int channel = 0; channel < 4; ++channel) {
        float u = 0.0f, v = 0.0f;
        if (!(x >> u) || !(y >> v) || std::fabs(u - v) > 0.5f / 255.0f + 1.0e-4f) return false;
    }
    return true;
}

template <typename T> ObjectPropertyCheck Check(const ObjectProperty<T> &property) {
    return {property.label, property.cid, [&property](CKObject *o) { return Text(property.get(o)); },
            [&property](CKObject *o) { property.set(o, property.other(property.get(o))); }, SameText<T>};
}

// Every property, once (the checks, the values a state does not keep).
template <typename F> void ForEachProperty(F f) {
    using namespace properties;
    f(CameraProjection);
    f(CameraAspectRatioActive);
    f(CameraOrthoZoom);
    f(CameraFov);
    f(CameraNearClip);
    f(CameraFarClip);
    f(LightSpecular);
    f(LightType);
    f(LightActive);
    f(LightColor);
    f(LightRange);
    f(LightPower);
    f(LightHotSpot);
    f(LightFallOff);
    f(LightFallOffShape);
    f(LightConstantAttenuation);
    f(LightLinearAttenuation);
    f(LightQuadraticAttenuation);
    f(MaterialAmbient);
    f(MaterialDiffuse);
    f(MaterialSpecular);
    f(MaterialEmissive);
    f(MaterialAlpha);
    f(MaterialPower);
    f(MaterialMode);
    f(MaterialSourceBlend);
    f(MaterialDestBlend);
    f(MaterialZWrite);
    f(MaterialAlphaBlend);
    f(MaterialZFunc);
    f(MaterialAlphaTest);
    f(MaterialAlphaFunc);
    f(MaterialAlphaRef);
    f(MaterialBothSided);
    f(MaterialFillMode);
    f(MaterialShadeMode);
    f(MaterialEffect);
    f(Entity3DPickable);
    f(Entity2DPickable);
    f(Entity2DHomogeneous);
    f(Entity2DClipToCamera);
    f(Entity2DRatioOffset);
    f(Entity2DZOrder);
    f(Entity2DBackground);
    f(Entity2DSourceRect);
    f(Entity2DClipToParent);
    f(Entity2DStickLeft);
    f(Entity2DStickRight);
    f(Entity2DStickTop);
    f(Entity2DStickBottom);
    f(SpriteVideoFormat);
    f(SpriteCurrentSlot);
    f(SpriteSaveOptions);
    f(SpriteTransparent);
    f(SpriteTransparentColor);
    f(TextureVideoFormat);
    f(TextureCurrentSlot);
    f(TextureSaveOptions);
    f(TextureTransparent);
    f(TextureTransparentColor);
    f(TextureMipmap);
    f(TextureCubeMap);
    f(TexturePickThreshold);
    f(MaterialTextureBlend);
    f(MaterialTextureMinMode);
    f(MaterialTextureMagMode);
    f(MaterialTextureAddress);
    f(MaterialTextureBorderColor);
    f(MaterialPerspectiveCorrect);
    f(MeshProgressive);
    f(MeshWrapMode);
    f(MeshLit);
    f(PatchMeshIterations);
    f(GridOrientation);
    f(GridPriority);
    f(LayerVisible);
    f(CurveOpen);
    f(CurveStepCount);
    f(CurveFitting);
    f(CurvePointBias);
    f(CurvePointTension);
    f(CurvePointContinuity);
    f(CurvePointLinear);
    f(SoundSaveOptions);
    f(WaveSoundType);
    f(WaveSoundLoop);
    f(WaveSoundGain);
    f(WaveSoundPitch);
    f(WaveSoundCone);
    f(WaveSoundDistance);
    f(Sprite3DMode);
    f(Sprite3DOffset);
    f(AnimationLength);
    f(AnimationLinked);
    f(AnimationFrameRate);
    f(AnimationMergeFactor);
    f(AnimationCanBeInterrupted);
    f(AnimationAlignOrientation);
    f(AnimationTransitionPreset);
    f(AnimationSecondaryPreset);
    f(AnimationTransitionFromNow);
    f(AnimationTransitionWarpFromCurrent);
    f(AnimationTransitionToStart);
    f(AnimationTransitionWarpToStart);
    f(AnimationTransitionWarpToBest);
    f(AnimationTransitionWarpToSamePosition);
    f(AnimationTransitionUseVelocity);
    f(AnimationTransitionLoopIfEqual);
    f(AnimationSecondaryPlay);
    f(AnimationSecondaryWarp);
    f(ObjectAnimationLength);
    f(ObjectAnimationMergeFactor);
    f(ObjectAnimationIgnorePosition);
    f(ObjectAnimationIgnoreRotation);
    f(ObjectAnimationIgnoreScale);
    f(ObjectAnimationIgnoreOffAxisScale);
    f(ObjectAnimationIgnoreMorph);
    f(SpriteTextText);
    f(SpriteTextColor);
    f(SpriteTextBackgroundColor);
    f(SpriteTextAlignment);
    f(SynchroUsers);
    f(StateActive);
}

} // namespace

std::vector<ObjectPropertyCheck> ObjectPropertyChecks() {
    std::vector<ObjectPropertyCheck> checks;
    ForEachProperty([&](const auto &property) {
        if (property.check) checks.push_back(Check(property));
    });
    return checks;
}

UnkeptValues ReadUnkeptValues(CKObject *object) {
    UnkeptValues values;
    if (!object) return values;
    ForEachProperty([&](const auto &property) {
        if (property.inState || !CKIsChildClassOf(object, property.cid)) return;
        const auto value = property.get(object);
        values.text += std::string(property.label) + "=" + Text(value) + ";";
        values.restore.push_back([&property, value](CKObject *target) { property.set(target, value); });
    });
    return values;
}

} // namespace vdev
