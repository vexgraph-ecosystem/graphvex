#ifndef GRAPHVEX_DEVICE_H
#define GRAPHVEX_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

// graphvex/device.h — DRAFT SKETCH (not wired): backend-agnostic GPU seam.
//
// Goal: darling's UI tree and hotcwap's window stop spelling Vk* types.
// graphvex owns devices, frames, and pipelines; each backend (Vulkan now,
// Metal/Direct later) is one driver row behind GraphicsBackend. Ditching MoltenVK
// then means adding metal.m — never touching darling.
//
// Split note (the Single Class Per File Law): this sketch holds 4 types in one header to keep the
// seam reviewable. On implementation each becomes its own pair:
// device.h, frame.h, drawable.h, backend.h.
//
// LAYER LAW: graphvex is vexspoke-only (the Vertical Integration Law). The drawable handle flows
// DOWN from hotcwap (it owns the NSWindow/CAMetalLayer/HWND); graphvex
// never includes window.h and never inspects the handle — drivers cast it.

// Opaque device: backend-private state lives behind this.
typedef struct GraphicsDevice GraphicsDevice;

// Per-frame command record, filled by beginFrame, consumed by endFrame.
typedef struct GraphicsFrame GraphicsFrame;

// Backend ids (for logging/selection; not bit flags).
#define GRAPHICS_BACKEND_VULKAN 1u
#define GRAPHICS_BACKEND_METAL  2u
#define GRAPHICS_BACKEND_DIRECT 3u

// Opaque drawable from the window owner (hotcwap). By value: a snapshot of
// WHAT to render into + HOW BIG in native pixels. Drivers cast handle:
//   Vulkan  -> VkSurfaceKHR * (MoltenVK today, direct swapchain tomorrow)
//   Metal   -> CAMetalLayer * (owned by hotcwap's NSWindow, never retained here)
//   Direct  -> IDXGISwapChain *
// graphvex code paths must treat handle as write-only transit: pass to the
// driver, never dereference, never retain/release.
typedef struct GraphicsDrawable {
    void *handle;
    uint32_t width;
    uint32_t height;
} GraphicsDrawable;

// Driver table: one const row per backend file (vulkan.c, metal.m,
// direct.c). Every entry is null-safe (null device = no-op, returns false).
typedef struct GraphicsBackend {
    uint32_t id;
    const char *name;
    bool (*init)(GraphicsDevice *dev);
    void (*shutdown)(GraphicsDevice *dev);
    bool (*beginFrame)(GraphicsDevice *dev, const GraphicsDrawable *target, GraphicsFrame *outFrame);
    bool (*submit)(GraphicsDevice *dev, GraphicsFrame *frame);
    bool (*present)(GraphicsDevice *dev, const GraphicsDrawable *target);
} GraphicsBackend;

// Selects a compiled-in driver row by GRAPHICS_BACKEND_* id; nullptr if the
// backend was not compiled in (e.g. Direct on macOS). Never aborts.
const GraphicsBackend *Graphics_selectBackend(uint32_t backendId);

// Device lifecycle. Create binds a backend row; destroy shuts it down.
// Both null-safe; create returns nullptr when backendId is unavailable.
GraphicsDevice *Graphics_create(const GraphicsBackend *backend);
void Graphics_destroy(GraphicsDevice *dev);

#endif
