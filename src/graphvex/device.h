#ifndef GRAPHVEX_DEVICE_H
#define GRAPHVEX_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

// graphvex/device.h — device/frame/drawable sketch (;;DRAFT, not wired).
//
// Goal: darling's UI tree and hotcwap's window stop spelling Vk* types.
// The UNIFIED SEAM is graphics/graphics.h: one Graphics table of drawable
// verbs with one const row per backend (VkGraphics, MetalGraphics,
// DirectGraphics) selected via Graphics_setGraphics. Backend ids live
// THERE (GRAPHICS_BACKEND_*), never re-declared in this file.
//
// Split note (the Single Class Per File Law): this sketch holds 3 types in one header to keep
// the seam reviewable. On implementation each becomes its own pair:
// device.h, frame.h, drawable.h. (backend.h became graphics/graphics.h.)
//
// LAYER LAW: graphvex is vexspoke-only (the Vertical Integration Law). The drawable handle flows
// DOWN from hotcwap (it owns the NSWindow/CAMetalLayer/HWND); graphvex
// never includes window.h and never inspects the handle — drivers cast it.

// Opaque device: backend-private state lives behind this.
typedef struct GraphicsDevice GraphicsDevice;

// Per-frame command record, filled by beginFrame, consumed by endFrame.
typedef struct GraphicsFrame GraphicsFrame;

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

#endif
