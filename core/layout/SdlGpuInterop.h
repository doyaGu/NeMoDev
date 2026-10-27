#pragma once

#include "CKTypes.h"

class CKRasterizerContext;
struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUTexture;

namespace vdev {

// Offered before Context creation. Unsupported rasterizers ignore it.
void OfferSdlGpuDevice(SDL_Window *window, SDL_GPUDevice *device);

// Imports one caller-owned SDL texture and binds its ordinary CK handle as
// the virtual backbuffer. Failure leaves no imported handle behind.
bool BindSdlGpuBackBuffer(CKRasterizerContext *context,
                          SDL_GPUDevice *device,
                          SDL_GPUTexture *texture,
                          int width, int height,
                          CKDWORD *outHandle);

// Removes the binding before deleting the imported handle. The caller may
// release its SDL texture only after this returns.
bool ReleaseSdlGpuBackBuffer(CKRasterizerContext *context,
                             CKDWORD *handle);

} // namespace vdev
