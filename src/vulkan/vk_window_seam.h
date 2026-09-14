#ifndef VK_WINDOW_SEAM_H
#define VK_WINDOW_SEAM_H

#include <stdbool.h>
#include <stdint.h>
#include "vulkan/vk.h"

// vulkan/vk_window_seam.h — the Window-system seam for Vulkan init/present.
//
// graphvex (the graphics foundation layer) must NEVER include headers from
// hotcwap, darling, or api-haven (the Vertical Integration Law). But the Vulkan instance/device
// needs to interact with the OS window for:
//   - CAMetalLayer extraction (surface creation)
//   - Present mode queries (FIFO vs IMMEDIATE pacing)
//   - Transparency state (clear color policy)
//   - Render generation tracking (swapchain rebuild triggers)
//   - Live resize detection (present deferral)
//   - Resize render hook installation
//   - Gravity policy (scissor anchoring)
//
// So: before calling Vk_init(), the host (hotcwap/kernel.c) calls
// Vk_setWindowSeam() to install opaque callbacks. graphvex then calls through
// the seam instead of touching Window* directly. This keeps graphvex
// zero-dependency on the OS layer — the seam is its only bridge.
//
// All window handles are void* to avoid const-correctness mismatches across
// the hotcwap Window_* API (some take const Window*, some take Window*).

// Accessor for the installed window handle (set by Vk_setWindowSeam).
void *Vk_seamWindow(void);

// Accessor for the installed CAMetalLayer provider (set by Vk_setWindowSeam).
void *Vk_seamMetalLayer(void);

// Seam accessors — graphvex calls these instead of Window_* directly.
// Each returns the value delegated through the callback registered by
// Vk_setWindowSeam. All return sensible defaults if no seam is installed
// (defensive: lets graphvex init fail gracefully rather than crash).
bool Vk_seamIsTransparent(void);
VkWindowPresentMode Vk_seamGetPresentMode(void);
uint64_t Vk_seamRenderGeneration(void);
bool Vk_seamIsLiveResizing(void);
void Vk_seamSetResizeRenderHook(void *fn, void *userdata);
void Vk_seamSetGravityTopLeft(void);

// Called by hotcwap's window layer to install the seam before Vk_init.
void Vk_setWindowSeam(
    void *window,
    void *(*metalLayerFn)(void *w),
    bool (*isTransparentFn)(void *w),
    VkWindowPresentMode (*getPresentModeFn)(void *w),
    uint64_t (*renderGenerationFn)(void *w),
    bool (*isLiveResizingFn)(void *w),
    void (*setResizeRenderHookFn)(void *w, void *fn, void *userdata),
    void (*setGravityTopLeftFn)(void *w)
);

#endif // VK_WINDOW_SEAM_H
