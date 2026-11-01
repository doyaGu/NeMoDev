#pragma once

#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include <cstdint>
#include "core/runtime/Bitmaps.h"

namespace vdev {
class UiTexture;
struct SchematicSnapshot;
class Document;
class SnapshotBuffer;

// The editor texture of a bitmap handle, made on first use; size in pixels.
// Null when the handle cannot be read.
ImTextureID SnapshotTexture(const std::shared_ptr<const SchematicSnapshot> &, ImVec2 *size);
ImTextureID SnapshotTexture(void *bitmap, ImVec2 *size, const std::shared_ptr<void> &owner = {});
#ifdef VIRTOOLSDEV_UI_TESTS
size_t SnapshotTextureCountForTests();
#endif

// A texture made from 32-bit BGRA rows (pitch bytes apart) under key,
// replacing the previous one of that key; alphaAsGray shows the alpha
// channel (Dev Texture Setup's second preview). Null when it fails.
ImTextureID PixelTexture(unsigned long long key, int width, int height, const void *bgra, int pitch, bool alphaAsGray);

// The texture last made by PixelTexture under key, or null.
ImTextureID CachedPixelTexture(unsigned long long key);

// Forgets every texture (a closed document frees its bitmap handles).
void ReleaseSnapshotTextures();

// A view replaced or forgotten during a frame may still be in its draw lists:
// released once the frame is presented (ReleaseRetiredTextures, Main.cpp).
void RetireTexture(std::unique_ptr<UiTexture> texture);
void ReleaseRetiredTextures();

} // namespace vdev
