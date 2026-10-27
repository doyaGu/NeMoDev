#include "core/layout/SdlGpuInterop.h"

#include <SDL3/SDL.h>

#include "CKRasterizer.h"
#include "CKSdlGpuRasterizer.h"

namespace vdev {

void OfferSdlGpuDevice(SDL_Window *window, SDL_GPUDevice *device) {
    if (window && device)
        SDL_SetPointerProperty(SDL_GetWindowProperties(window),
                               CKSDL_GPU_DEVICE_PROPERTY, device);
}

bool BindSdlGpuBackBuffer(CKRasterizerContext *context,
                          SDL_GPUDevice *device,
                          SDL_GPUTexture *texture,
                          int width, int height,
                          CKDWORD *outHandle) {
    if (outHandle) *outHandle = 0;
    if (!context || !device || !texture || width <= 0 || height <= 0 ||
        !outHandle)
        return false;

    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ABGR8888, desc.Format);
    desc.Format.Width = width;
    desc.Format.Height = height;
    desc.MipMapCount = 1;
    desc.Depth = 1;
    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB |
                 CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;

    CKDWORD handle = 0;
    if (!CKSdlGpuImportTexture(context, &desc, device, texture, &handle) ||
        !handle)
        return false;
    if (!context->SetBackBufferTexture(handle)) {
        context->DeleteObject(handle, CKRST_OBJ_TEXTURE);
        return false;
    }
    *outHandle = handle;
    return true;
}

bool ReleaseSdlGpuBackBuffer(CKRasterizerContext *context,
                             CKDWORD *handle) {
    if (!handle || !*handle)
        return true;
    const CKDWORD texture = *handle;
    if (!context || !context->SetBackBufferTexture(0) ||
        !context->DeleteObject(texture, CKRST_OBJ_TEXTURE))
        return false;
    *handle = 0;
    return true;
}

} // namespace vdev
