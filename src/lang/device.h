#ifndef LANG_DEVICE_H
#define LANG_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

// lang/device.h — the graphics device contract (the language of graphics).
//
// THE DEVICE IS THE FIRST WORD OF THE LANGUAGE. Everything else in graphvex —
// images, buffers, surfaces, pipelines, the drawable verbs — is spoken THROUGH
// a device. This header is the backend-agnostic vocabulary: a caller names a
// backend id and a window handle, and gets an opaque Device back. It never
// spells VkDevice, id<MTLDevice>, ID3D12Device, or wgpu::Device.
//
// DIALECTS (one implementation per directory, all behind this contract):
//   vulkan/  -> VkInstance / VkPhysicalDevice / VkDevice / VkQueue   (MoltenVK on macOS)
//   metal/   -> MTLDevice / MTLCommandQueue
//   d3d/     -> ID3D12Device / ID3D12CommandQueue
//   wgpu/    -> WGPUDevice / WGPUQueue
//   raster/  -> CPU-side software rasterizer (also the headless/CI dialect)
//   null/    -> a no-op dialect (tests, deterministic teardown proofs)
//
// LAYER LAW (the Vertical Integration Law): graphvex is vexspoke-only. The
// window handle flows DOWN from the window owner (hotcwap R1); graphvex never
// includes window/window.h and never inspects the handle — the dialect casts
// it. By value, write-only transit: pass it to the dialect, never dereference,
// never retain.
//
// NAMING: `Device` (not `GraphicsDevice`) — the old reference's
// GraphicsDevice/GraphicsFrame/GraphicsDrawable sketch is ported here and
// split per the Single Class Per File Law as the implementation lands
// (device.h, frame.h, drawable.h).

// Backend selection ids (not bit flags). Canonical here; no dialect re-declares
// them, and the unified Graphics table (lang/graphics.h) references these.
#define LANG_BACKEND_NONE   0u
#define LANG_BACKEND_VULKAN 1u
#define LANG_BACKEND_METAL  2u
#define LANG_BACKEND_D3D    3u
#define LANG_BACKEND_WGPU   4u
#define LANG_BACKEND_RASTER 5u
#define LANG_BACKEND_NULL   6u

// Opaque device: dialect-private state lives behind this handle. The caller
// only ever holds the pointer; the dialect defines the struct.
typedef struct Device Device;

// The window-owned drawable a device renders into. A by-value snapshot of WHAT
// to render into (an opaque OS handle) and HOW BIG in NATIVE hardware pixels
// (the Native Pixel Law). The dialect casts the handle:
//   Vulkan -> the CAMetalLayer / HWND the seam surface is built from
//   Metal  -> CAMetalLayer * (owned by the window, never retained here)
//   D3D    -> HWND
//   WGPU   -> the surface descriptor source
// width/height are native px, never logical points.
typedef struct Drawable {
    void *handle;
    uint32_t width;
    uint32_t height;
} Drawable;

// Device construction parameters. Every field has a default; a call site names
// only what it wants to change. Zero it for pure defaults (backend NONE is an
// error — a device must name its dialect).
typedef struct DeviceDesc {
    uint32_t backend;    // LANG_BACKEND_* (default NONE = fail-closed)
    void *window;        // window-owned OS handle (default nullptr = offscreen)
    uint32_t width;      // native px (default 0 = offscreen/unsized)
    uint32_t height;     // native px
} DeviceDesc;

// --- Constructors (the arity-overloaded chooser idiom) ---
//
//   Device(LANG_BACKEND_VULKAN, window)         -> sized from the window
//   Device_new(&(DeviceDesc){ ... })            -> every other field
//
// A device constructs in a usable state or returns nullptr (the Cold-Strict,
// Hot-Minimal Validation Law: fail closed, never a half-device).
Device *Device_0(void);
Device *Device_2(uint32_t backend, void *window);
Device *Device_new(const DeviceDesc *desc);

#define DEVICE_CHOOSER(_0, _1, _2, NAME, ...) NAME
#define Device(...) DEVICE_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Device_2, Device_1, Device_0 \
)(__VA_ARGS__)

// Destroy the device and every resource it owns. Null-safe. Top-down per the
// Teardown Order Law (surfaces/images/buffers die before the device).
void Device_destroy(Device *device);

// --- Core functions ---
// Present the drawable: the device's one on-demand present entry (the
// Present-On-Demand Law). Returns false when not ready, minimized, or the
// device is lost — never blocks unbounded (the Bounded Wait Law).
bool Device_present(Device *device);

// Rebind the drawable extent in native px (cold path: window attach/resize).
bool Device_resize(Device *device, uint32_t width, uint32_t height);

// --- Getters (the Symmetric Getter/Setter Completeness Law: null-safe) ---
uint32_t Device_backend(const Device *device);   // LANG_BACKEND_*, NONE on null
bool     Device_isValid(const Device *device);
bool     Device_isReady(const Device *device);   // device up and not lost
uint32_t Device_width(const Device *device);     // native px
uint32_t Device_height(const Device *device);    // native px
void    *Device_native(const Device *device);    // dialect handle, opaque transit

#endif // LANG_DEVICE_H
