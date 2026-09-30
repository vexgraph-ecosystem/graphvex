#ifndef GRAPHICS_VULKAN_BACKEND_H
#define GRAPHICS_VULKAN_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "graphics/graphics.h"
#include "vulkan/vk_batch.h"

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

// The live quad batch (tests/debug); valid between begin() and present().
const VkBatch *VulkanBackend_batch(void);
const char *VulkanBackend_lastError(void);

#endif // GRAPHICS_VULKAN_BACKEND_H
