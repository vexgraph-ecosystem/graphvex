#ifndef VULKAN_VK_GRAPHICS_H
#define VULKAN_VK_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "graphics/graphics.h"
#include "graphvex/type.h"

// vulkan/vk_graphics.h — VkGraphics: the Vulkan backend row.
//
// Single Class Per File Law: VkGraphics.
//
// VkGraphics is the live hardware row of the unified Graphics seam: it
// adapts the existing Vulkan loader seam (vulkan/vk.h — MoltenVK on macOS
// today, direct swapchains on Windows tomorrow) onto the one Graphics
// table. Selecting it makes the whole app speak through Graphics_* — the
// same unified system on macOS, Windows, or any API, because presentation
// code only ever talks to the one table (the Pixel Coordinate Contract:
// every verb takes native hardware pixels; NDC exists only inside vertex
// shaders).
//
// Frame lifecycle mapping (the seam's Vk_clearPresent is atomic
// acquire->clear->present; the row splits that into table verbs):
//   begin    Vk_ready() gating; opens the frame window
//   clear    stages 0xAARRGGBB into Vk_setClearColor; ready device only
//   present  demand-present: runs Vk_clearPresent once when a clear was
//            staged since the last present, else rests (the Present-On-
//            Demand Law — clean content is never re-presented)
//   end      closes the frame window and releases the bound command buffer
//   resize   records the newest native-pixel extent; the seam rebuilds the
//            swapchain out-of-date inside its own present path (the
//            Continuous Real-Time Live Resize Law)
//
// Drawable verbs record into the command buffer bound by
// VkGraphics_bindFrame (the seam's swapchain render-pass callback hands the
// live command buffer to this row once per present). fillRect/drawRect are
// LIVE over the Vk_fillRect primitive (the exact quad the Panel rows paint
// with — one primitive, two table verbs); circles/path/image remain ;;DRAFT
// until their tessellation / bindless-texture plumbing lands (cold-false).
//
// Lifecycle: one file-local process-global value, zero steady-state
// allocation. Before a device is up (Vk_ready false) every device-backed
// call cold-returns false; selection itself only needs the row registered
// via VkGraphics_0 (call after Vk_init, before Graphics_setGraphics).

typedef struct VkGraphics {
    uint32_t width;       // newest native-px drawable extent; 0 until resize
    uint32_t height;      // newest native-px drawable extent
    uint32_t clearColor;  // staged 0xAARRGGBB for the next demand-present
    bool clearPending;    // a clear staged since the last present
    bool frameOpen;       // begin() succeeded and end() has not run
    void *boundCmdBuffer; // live seam command buffer (VkGraphics_bindFrame); null until bound
    uint32_t boundW;      // drawable extent of the bound pass, native px
    uint32_t boundH;      // drawable extent of the bound pass, native px
} VkGraphics;

// The row: pass to Graphics_setGraphics via GRAPHICS_BACKEND_VULKAN.
// NULL until VkGraphics_0 registers the process-global singleton.
const Graphics *VkGraphics_getRow(void);

// Register the process-global singleton. Idempotent: the first call owns
// registration; later calls re-bind the same object. Returns the
// singleton (never null).
VkGraphics *VkGraphics_0(void);

// Bind/rebind the drawable extent in native px (cold path, window
// attach/resize). Returns false on zero dims or a latched dead device
// (Vk_isDeviceLost).
bool VkGraphics_resize(uint32_t width, uint32_t height);

// Bind the seam's live command buffer for one swapchain render pass (called
// by the host's frame-renderer callback — the conduit that hands the open
// render pass to this row; the Vk_clearPresent atomic calls it inside the
// pass). Records the drawable extent + opens the frame window. Returns
// false on null buffer, zero extent, or a dead device. Idempotent re-bind
// per present is the normal shape (one bind per frame callback).
bool VkGraphics_bindFrame(void *cmdBuffer, uint32_t width, uint32_t height);

// Null-safe inspectors (before registration yields 0 / false):
uint32_t VkGraphics_getWidth(void);
uint32_t VkGraphics_getHeight(void);
uint32_t VkGraphics_getClearColor(void);
bool VkGraphics_isClearPending(void);
bool VkGraphics_isFrameOpen(void);
bool VkGraphics_isReady(void);

#endif