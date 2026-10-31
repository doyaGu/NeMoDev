#pragma once

#include "core/document/EditResult.h"
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "CKAll.h"

namespace vdev {

// A property the Setups edit (Dev's Camera, Light and Material Setups), in
// CK's units (radians, CK's enum values): read and written as the Setup does.
// The label is the Setup's and its undo step's. The Setups convert for
// display (degrees, their lists' order) and keep the edit live
// (the registered property draft previews the value before one undo step).
// other: a valid value differing from the one given (the checks over every
// property, ObjectPropertyChecks). inState: CKSaveObjectState keeps the
// value; when it does not, the value travels with the object's state
// (ReadUnkeptValues) and is set back once the state is read. check: the
// checks can set it without a device (the texture's mipmaps exist in video
// memory, a wave sound's type with a sound source only).
template <typename T> struct ObjectProperty {
    const char *label;
    CK_CLASSID cid;
    T (*get)(CKObject *object);
    void (*set)(CKObject *object, T value);
    T (*other)(T value);
    bool inState = true;
    bool check = true;
    CK_CLASSID referenceClass = CKCID_OBJECT;
};

namespace properties {

extern const ObjectProperty<int> CameraProjection;       // CK_PERSPECTIVEPROJECTION / CK_ORTHOGRAPHICPROJECTION
extern const ObjectProperty<bool> CameraAspectRatioActive; // not CK_3DENTITY_CAMERAIGNOREASPECT
extern const ObjectProperty<float> CameraOrthoZoom;
extern const ObjectProperty<float> CameraFov; // radians
extern const ObjectProperty<float> CameraNearClip;
extern const ObjectProperty<float> CameraFarClip;

extern const ObjectProperty<bool> LightSpecular;
extern const ObjectProperty<int> LightType; // VXLIGHT_TYPE
extern const ObjectProperty<bool> LightActive;
extern const ObjectProperty<VxColor> LightColor; // the alpha is CK's
extern const ObjectProperty<float> LightRange;
extern const ObjectProperty<float> LightPower;
extern const ObjectProperty<float> LightHotSpot; // radians
extern const ObjectProperty<float> LightFallOff; // radians
extern const ObjectProperty<float> LightFallOffShape;
extern const ObjectProperty<float> LightConstantAttenuation;
extern const ObjectProperty<float> LightLinearAttenuation;
extern const ObjectProperty<float> LightQuadraticAttenuation;

// The colors' rgb; each keeps its alpha (the diffuse's is MaterialAlpha).
extern const ObjectProperty<VxColor> MaterialAmbient;
extern const ObjectProperty<VxColor> MaterialDiffuse;
extern const ObjectProperty<VxColor> MaterialSpecular;
extern const ObjectProperty<VxColor> MaterialEmissive;
extern const ObjectProperty<int> MaterialAlpha; // the diffuse's, 0-255 (the state keeps 8 bits)
extern const ObjectProperty<float> MaterialPower;
// Dev's Mode (Dev.exe sub_46A6E0 reads it, sub_4634E0 712 sets it): 0
// Opaque (no blend, One / Zero, Z write, Z <=, no alpha test), 1
// Transparent (blend Src Alpha / Inv Src Alpha, no Z write, Z <=, no alpha
// test), 2 Mask (the same with Z write and an alpha test >= 1), 3 Custom
// (any other; setting it changes nothing).
extern const ObjectProperty<int> MaterialMode;
extern const ObjectProperty<int> MaterialSourceBlend; // VXBLEND_MODE
extern const ObjectProperty<int> MaterialDestBlend;   // VXBLEND_MODE
extern const ObjectProperty<bool> MaterialZWrite;
extern const ObjectProperty<bool> MaterialAlphaBlend;
extern const ObjectProperty<int> MaterialZFunc;     // VXCMPFUNC
extern const ObjectProperty<bool> MaterialAlphaTest;
extern const ObjectProperty<int> MaterialAlphaFunc; // VXCMPFUNC
extern const ObjectProperty<int> MaterialAlphaRef;  // 0-255
extern const ObjectProperty<bool> MaterialBothSided;
extern const ObjectProperty<int> MaterialFillMode;  // VXFILL_MODE
extern const ObjectProperty<int> MaterialShadeMode; // VXSHADE_MODE (the state does not keep Phong)
extern const ObjectProperty<int> MaterialEffect;    // VX_EFFECT (the state does not keep it)

// The other Setups' properties (round 68): 3D and 2D entities, 2D sprites and
// textures (their bitmaps), materials' texture modes, meshes, grids and their
// layers, curves and curve points, sounds, 3D sprites, animations and object
// animations, sprite texts, synchros and states. The enum values are CK's;
// colors without alpha keep CK's 1; a Building Block's flag bits each a
// property (labelled as its Setup's undo step).
extern const ObjectProperty<bool> Entity3DPickable;
extern const ObjectProperty<bool> Entity2DPickable;
extern const ObjectProperty<bool> Entity2DHomogeneous;
extern const ObjectProperty<bool> Entity2DClipToCamera;
extern const ObjectProperty<bool> Entity2DRatioOffset;
extern const ObjectProperty<int> Entity2DZOrder;
extern const ObjectProperty<bool> Entity2DBackground;
extern const ObjectProperty<bool> Entity2DSourceRect;
extern const ObjectProperty<bool> Entity2DClipToParent;
extern const ObjectProperty<bool> Entity2DStickLeft;
extern const ObjectProperty<bool> Entity2DStickRight;
extern const ObjectProperty<bool> Entity2DStickTop;
extern const ObjectProperty<bool> Entity2DStickBottom;
extern const ObjectProperty<int> SpriteVideoFormat;
extern const ObjectProperty<int> SpriteCurrentSlot;
extern const ObjectProperty<int> SpriteSaveOptions;
extern const ObjectProperty<bool> SpriteTransparent;
extern const ObjectProperty<VxColor> SpriteTransparentColor;
extern const ObjectProperty<int> TextureVideoFormat;
extern const ObjectProperty<int> TextureCurrentSlot;
extern const ObjectProperty<int> TextureSaveOptions;
extern const ObjectProperty<bool> TextureTransparent;
extern const ObjectProperty<VxColor> TextureTransparentColor;
extern const ObjectProperty<int> TextureMipmap;
extern const ObjectProperty<bool> TextureCubeMap;
extern const ObjectProperty<int> TexturePickThreshold;
extern const ObjectProperty<int> MaterialTextureBlend;
extern const ObjectProperty<int> MaterialTextureMinMode;
extern const ObjectProperty<int> MaterialTextureMagMode;
extern const ObjectProperty<int> MaterialTextureAddress;
extern const ObjectProperty<VxColor> MaterialTextureBorderColor; // ARGB
extern const ObjectProperty<bool> MaterialPerspectiveCorrect;
extern const ObjectProperty<bool> MeshProgressive;
extern const ObjectProperty<int> MeshWrapMode;
extern const ObjectProperty<bool> MeshLit;
extern const ObjectProperty<int> PatchMeshIterations;
extern const ObjectProperty<int> GridOrientation;
extern const ObjectProperty<int> GridPriority;
extern const ObjectProperty<bool> LayerVisible;
extern const ObjectProperty<bool> CurveOpen;
extern const ObjectProperty<int> CurveStepCount;
extern const ObjectProperty<float> CurveFitting;
extern const ObjectProperty<float> CurvePointBias;
extern const ObjectProperty<float> CurvePointTension;
extern const ObjectProperty<float> CurvePointContinuity;
extern const ObjectProperty<bool> CurvePointLinear;
extern const ObjectProperty<int> SoundSaveOptions;
extern const ObjectProperty<int> WaveSoundType;
extern const ObjectProperty<bool> WaveSoundLoop;
extern const ObjectProperty<float> WaveSoundGain;
extern const ObjectProperty<float> WaveSoundPitch;
extern const ObjectProperty<VxVector> WaveSoundCone;
extern const ObjectProperty<Vx2DVector> WaveSoundDistance;
extern const ObjectProperty<int> Sprite3DMode;
extern const ObjectProperty<Vx2DVector> Sprite3DOffset;
extern const ObjectProperty<float> AnimationLength;
extern const ObjectProperty<bool> AnimationLinked;
extern const ObjectProperty<float> AnimationFrameRate;
extern const ObjectProperty<float> AnimationMergeFactor;
extern const ObjectProperty<bool> AnimationCanBeInterrupted;
extern const ObjectProperty<bool> AnimationAlignOrientation;
extern const ObjectProperty<bool> AnimationTransitionPreset;
extern const ObjectProperty<bool> AnimationSecondaryPreset;
extern const ObjectProperty<bool> AnimationTransitionFromNow;
extern const ObjectProperty<bool> AnimationTransitionWarpFromCurrent;
extern const ObjectProperty<bool> AnimationTransitionToStart;
extern const ObjectProperty<bool> AnimationTransitionWarpToStart;
extern const ObjectProperty<bool> AnimationTransitionWarpToBest;
extern const ObjectProperty<bool> AnimationTransitionWarpToSamePosition;
extern const ObjectProperty<bool> AnimationTransitionUseVelocity;
extern const ObjectProperty<bool> AnimationTransitionLoopIfEqual;
extern const ObjectProperty<int> AnimationSecondaryPlay;
extern const ObjectProperty<bool> AnimationSecondaryWarp;
extern const ObjectProperty<float> ObjectAnimationLength;
extern const ObjectProperty<float> ObjectAnimationMergeFactor;
extern const ObjectProperty<bool> ObjectAnimationIgnorePosition;
extern const ObjectProperty<bool> ObjectAnimationIgnoreRotation;
extern const ObjectProperty<bool> ObjectAnimationIgnoreScale;
extern const ObjectProperty<bool> ObjectAnimationIgnoreOffAxisScale;
extern const ObjectProperty<bool> ObjectAnimationIgnoreMorph;
extern const ObjectProperty<std::string> SpriteTextText;
extern const ObjectProperty<VxColor> SpriteTextColor;
extern const ObjectProperty<VxColor> SpriteTextBackgroundColor;
extern const ObjectProperty<int> SpriteTextAlignment;
extern const ObjectProperty<int> SynchroUsers;
extern const ObjectProperty<bool> StateActive;

extern const ObjectProperty<VxVector> Entity3DWorldPosition;
extern const ObjectProperty<VxVector> Entity3DLocalPosition;
extern const ObjectProperty<VxVector> Entity3DWorldOrientation;
extern const ObjectProperty<VxVector> Entity3DLocalOrientation;
extern const ObjectProperty<VxVector> Entity3DWorldScale;
extern const ObjectProperty<VxVector> Entity3DLocalScale;
extern const ObjectProperty<Vx2DVector> CameraRatio;
extern const ObjectProperty<CK_ID> CameraTarget;
extern const ObjectProperty<CK_ID> LightTarget;
extern const ObjectProperty<CK_ID> Entity2DMaterial;
extern const ObjectProperty<CK_ID> Entity2DParent;
extern const ObjectProperty<CK_ID> MaterialTexture;
extern const ObjectProperty<CK_ID> Entity3DCurrentMesh;
extern const ObjectProperty<CK_ID> Sprite3DMaterial;
extern const ObjectProperty<CK_ID> KinematicStart;
extern const ObjectProperty<CK_ID> KinematicEnd;
extern const ObjectProperty<Vx2DVector> Entity2DPosition;
extern const ObjectProperty<Vx2DVector> Entity2DSize;
extern const ObjectProperty<VxRect> Entity2DUv;
extern const ObjectProperty<Vx2DVector> Sprite3DSize;
extern const ObjectProperty<VxRect> Sprite3DUv;
extern const ObjectProperty<VxVector> CurvePointPosition;
extern const ObjectProperty<bool> WaveSoundStreaming;
extern const ObjectProperty<float> WaveSoundPan;
extern const ObjectProperty<CK_ID> WaveSoundAttached;
extern const ObjectProperty<VxVector> WaveSoundPosition;
extern const ObjectProperty<VxVector> WaveSoundDirection;
extern const ObjectProperty<int> MeshVerticesRendered;
extern const ObjectProperty<bool> MeshGeoMorph;
extern const ObjectProperty<int> MeshGeoMorphStep;
extern const ObjectProperty<bool> MeshStripify;
} // namespace properties

// Stable property names are shared by commands and the native state checks.
template <typename F> void ForEachObjectProperty(F f) {
#define VDEV_OBJECT_PROPERTY(Name, Id) f(Id, properties::Name);
#include "core/level/ObjectProperties.def"
#undef VDEV_OBJECT_PROPERTY
}
const char *ObjectPropertyId(const void *property);

// Every property, for checks over all of them: its value as text, setting
// another one (ObjectProperty::other), and whether two texts are the same
// value (colors to the 8 bits a state keeps of each channel).
struct ObjectPropertyCheck {
    const char *label;
    CK_CLASSID cid;
    std::function<std::string(CKObject *)> text;
    std::function<void(CKObject *)> setOther;
    std::function<bool(const std::string &, const std::string &)> same;
};
std::vector<ObjectPropertyCheck> ObjectPropertyChecks();

// An object's values of the properties its state does not keep
// (ObjectProperty::inState false), as text (to tell a change) and as setters
// putting them back. Object edits keep them with the native state.
struct UnkeptValues {
    std::string text;
    std::vector<CKObjectHandle> references;
    std::vector<std::function<void(CKObject *)>> restore;
};
UnkeptValues ReadUnkeptValues(CKObject *object);

// Value records are useful where restoring a whole native object would also
// normalize unrelated state, such as a bitmap without image slots.
class Document;
using ObjectPropertyValue =
    std::variant<bool, int, float, std::string, VxColor, VxVector, Vx2DVector, VxRect, CKObjectHandle>;
struct ObjectPropertyState {
    CKObjectHandle object{};
    ObjectPropertyValue value = false;
    std::string text;
};
bool ReadObjectProperty(Document &document, const char *property, const std::vector<CK_ID> &objects,
                        std::vector<ObjectPropertyState> &values);
bool RestoreObjectProperty(Document &document, const char *property, const std::vector<ObjectPropertyState> &values);
EditResult CommitObjectProperty(Document &document, const char *property,
                                const std::vector<ObjectPropertyState> &before, const std::string &label);

} // namespace vdev
