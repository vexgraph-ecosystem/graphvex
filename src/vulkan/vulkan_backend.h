#ifndef GRAPHICS_VULKAN_BACKEND_H
#define GRAPHICS_VULKAN_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "graphics/graphics.h"
#include "vulkan/vk_batch.h"
#include "vulkan/device.h"

// graphvex R3 — vulkan/vulkan_backend.h
//
// The Vulkan implementation of the one Backend seam. Callers never see a
// VkHandle: bind a native layer, register the row, draw rects. No swapchain:
// the destination is a host-borrowed seam (a CAMetalLayer on Apple; the host
// window elsewhere); we render into our own Images/Boards.

#define BACKEND_VULKAN 2u

// Bind to a host-provided native destination:
//   - Apple    : `nativeLayer` is a borrowed CAMetalLayer (id<CAMetalLayer>)
//   - Win/Linux: `nativeWindow` is the platform handle (HWND / xcb window)
bool VulkanBackend_bind(void *nativeLayer, uint32_t widthPx, uint32_t heightPx);
void VulkanBackend_unbind(void);
const Backend *VulkanBackend_row(void);

// Zero-copy seam (Apple, VK_EXT_metal_objects): import a host IOSurface as the
// render target. `iosurface` is a borrowed IOSurfaceRef (RGBA8, native px); we
// never free it. Present renders straight into it — no readback. Unbind returns
// to the private (readback) target. False when unsupported (non-Apple/driver).
bool VulkanBackend_bindSurface(void *iosurface, uint32_t widthPx, uint32_t heightPx);
void VulkanBackend_unbindSurface(void);

// A pool of imported surface targets (double buffering): add a surface once,
// then select which one present renders into. add returns a slot (>=0) or -1.
// use(-1) returns to the private readback target. cleanup releases them all.
int  VulkanBackend_addSurface(void *iosurface, uint32_t widthPx, uint32_t heightPx);
bool VulkanBackend_useSurface(int slot);
void VulkanBackend_removeSurface(int slot);
void VulkanBackend_cleanupSurfaces(void);

// The live quad batch (tests/debug); valid between begin() and present().
const VkBatch *VulkanBackend_batch(void);
const char *VulkanBackend_lastError(void);

/* Cold owner-thread seam. The returned Device is borrowed from the renderer;
 * it remains alive until unbind. Prepare uploads a CPU Image once, or admits a
 * completed texture from that same Device. Images must clear/release GPU bindings
 * before unbind. Repainting reads retained descriptors, never CPU color runs.
 * Mutating raw CPU pixels requires Image_clearGpu before the next preparation. */
Device *VulkanBackend_device(void);
bool VulkanBackend_prepareImage(Image *image);
size_t VulkanBackend_vertexBytes(void);

#endif // GRAPHICS_VULKAN_BACKEND_H
