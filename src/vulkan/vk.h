#ifndef VULKAN_VK_H
#define VULKAN_VK_H

#include <stdbool.h>
#include <stdint.h>

// vulkan/vk.h — the GPU backend seam (MoltenVK on macOS, software stays).
//
// Runtime loading only: the loader dylib is dlopen'd, every entry point is
// fetched through vkGetInstanceProcAddr. No link-time dependency, so a machine
// without MoltenVK degrades gracefully to the software path.
//
// Milestone contract: init builds the chain instance -> surface -> device ->
// swapchain; clearPresent acquires an image, clears it to a solid color and
// presents. THREAD CONTRACT: init and clearPresent run on thread 0 (the
// surface wraps the window's AppKit view).
//
// Window-system seam (the Vertical Integration Law): graphvex must NEVER include hotcwap/window.h.
// Instead, the host calls Vk_setWindowSeam() before Vk_init(), registering
// opaque callbacks. All Window_* queries in the Vulkan layer go through these.

typedef struct VkWindow VkWindow; // opaque marker type; actual handle is void*

// Present mode enum (mirrors Window's WINDOW_PRESENT_* values)
typedef enum {
    VK_WINDOW_PRESENT_FIFO      = 0, // display-synced, capped (default)
    VK_WINDOW_PRESENT_IMMEDIATE = 1, // uncapped, no sync
    VK_WINDOW_PRESENT_MAILBOX   = 2, // triple-buffered low-latency
} VkWindowPresentMode;

// Window seam callbacks — hotcwap registers these before Vk_init.
// Uses void* for the window handle to avoid const-correctness mismatches
// across the hotcwap Window_* API (some take const Window*, some take Window*).
void Vk_setWindowSeam(
    void *window,
    void *(*metalLayerFn)(void *w),
    bool (*isTransparentFn)(void *w),
    VkWindowPresentMode (*getPresentModeFn)(void *w),
    uint64_t (*renderGenerationFn)(void *w),
    bool (*isLiveResizingFn)(void *w),
    void (*setResizeRenderHookFn)(void *w, void *fn, void *userdata),
    void (*setGravityTopLeftFn)(void *w),
    bool (*isMinimizedFn)(void *w)
);

// Init: Vk_setWindowSeam must be called first. Returns false if the
// loader/seam is absent or the device fails to materialize.
bool Vk_init(void);
void Vk_shutdown(void);
bool Vk_ready(void);

// The seam swapchain's CURRENT image extent — the pixels the next present
// will actually put on screen. Zeroes before the chain exists.
void Vk_seamExtent(int32_t *outW, int32_t *outH);

// The extent the SEAM OWNER wants RENDERED next (the frame's authoritative
// live drawable px, published every geometry step). In the fixed-buffer model
// this is the RENDER AREA, not the chain size: the chain is allocated ONCE at
// Vk_seamSetMaxExtent and never rebuilt for a window resize, and each present
// scissors/viewports + render-areas to this region. The CAMetalLayer's
// kCAGravityTopLeft then crops the big drawable 1:1 to the window bounds —
// no scaling, no strip, no per-step swapchain churn. Zero = whole chain.
void Vk_seamSetExtent(int32_t widthPx, int32_t heightPx);

// Allocate the seam chain ONCE at this extent (native px) — the monitor-sized
// buffer the window is plastered onto. Clamped into the surface's supported
// range; usually the display's native pixel size. Call before Vk_init (or any
// time: it applies at the next chain build) from the platform seam.
void Vk_seamSetMaxExtent(int32_t widthPx, int32_t heightPx);

// Terminal device-loss latch: true once the driver reports
// VK_ERROR_DEVICE_LOST. The VkDevice is dead from that point — presents
// short-circuit and only a restart recovers. Lets the title report
// "device lost" instead of a lying "idle".
bool Vk_isDeviceLost(void);

// the Ecosystem Vulkan Safety Nets Law flight probe: true only when the present submit fence is signaled —
// the board present CB, which may have sampled bindless textures via the frame
// renderer, is no longer executing. Non-blocking GetFenceStatus poll; the fence
// starts SIGNALED so a never-presented device reads idle. Consumed by the
// texture-retire guard: FreeMemory under a flying present Submit is the
// GPU-page-fault defect, so texture destruction defers while a present flies.
bool Vk_presentFlightIdle(void);

// True only when VK_EXT_debug_utils is actually enabled on the live device.
// Callers use this to gate vkSetDebugUtilsObjectNameEXT — the loader resolves
// the symbol even on unsupported builds, but calling it on a non-debug device
// segfaults (the Ecosystem Vulkan Safety Nets Law seam naming).
bool Vk_isDebugUtilsEnabled(void);

// Acquire, clear the monitor cache to the window's background color (or the
// basket panel's own color while one is set), render ONE layer — the direct
// children of the window's container basket — at absolute desktop
// coordinates, then blit the window's region (top-left anchored) from the
// monitor's giant cache into the acquired swapchain image and present.
// False when not ready or the swapchain is _out of date. Present pacing
// follows the seam's GetPresentMode callback.
bool Vk_clearPresent(void);

// Live-drag variant of Vk_clearPresent: identical single-frame contract
// (same guards, same 100ms fence / 25ms acquire bounds, dirty retained on
// drop), except the present-lock acquisition retries in ~1ms slices up to
// an ~8ms budget (half a 60Hz vsync) before dropping — the modal drag step
// on thread 0 must plaster under worker contention without freezing the
// modal loop (the Bounded Wait Law). Consumed by GraphicsLoop_modalTickForced.
bool Vk_clearPresentLive(void);

// Phase-3 live shader reload: rebuild tri/quad pipelines from current
// .spv bytes with no device/surface/window teardown (tex/sdf rebuild
// lazily on next draw). Try-lock: false means the worker owned the tick,
// retry later. Re-baseline file watchers only on true.
bool Vk_reloadShaders(void);

// Pre-frame callback hook invoked before swapchain acquisition (e.g. offscreen passes).
typedef void (*VkPreFrameFn)(void *window, int drawW, int drawH, void *userdata);
void Vk_setPreFrameRenderer(VkPreFrameFn fn, void *userdata);

// Frame render callback hook invoked inside swapchain render pass.
typedef void (*VkFrameRenderFn)(void *cmdBuffer, int drawW, int drawH, void *userdata);
void Vk_setFrameRenderer(VkFrameRenderFn fn, void *userdata);

// Set swapchain background clear color (0.0f - 1.0f RGBA).
void Vk_setClearColor(float r, float g, float b, float a);

// Human-readable stop point of the last init attempt ("ok", "no loader", ...).
const char *Vk_status(void);

// Solid-quad primitive: the default Panel draw, exposed so Panel_RenderFn
// overrides can compose real content _out of it ("pointing one function at
// another"). Records an axis-aligned fill at drawable-pixel coords into an
// open render pass; sets its own viewport (whole drawable) and scissor (the
// rect), per the VIEWPORT/SCISSOR SEPARATION LAW. Safe to call several times
// per handler for layered rects.
void Vk_fillRect(void *cmdBuffer, float surfaceW, float surfaceH, float x, float y, float w, float h,
                 float r, float g, float b, float a);

// Format + render pass currently backing the window's swapchain. The seam
// canvas is the window's only swapchain, so every existing pipeline binds
// unchanged.
unsigned int Vk_getFormat(void);
void *Vk_getDrawablePass(void);

// Unified picture mode combining scaling strategy and 1:1 fill anchors.
typedef enum {
    PICTURE_MODE_FIT               = 0, // Stretch to fill
    PICTURE_MODE_ZOOM_FILL         = 1, // Scale to cover (centered)
    PICTURE_MODE_ZOOM_FIT          = 2, // Scale to contain (centered)
    PICTURE_MODE_FILL_CENTER       = 3, // 1:1 pixel mapping, centered
    PICTURE_MODE_FILL_TOP_LEFT     = 4, // 1:1 pixel mapping, top-left anchor
    PICTURE_MODE_FILL_TOP_RIGHT    = 5, // 1:1 pixel mapping, top-right anchor
    PICTURE_MODE_FILL_BOTTOM_LEFT  = 6, // 1:1 pixel mapping, bottom-left anchor
    PICTURE_MODE_FILL_BOTTOM_RIGHT = 7, // 1:1 pixel mapping, bottom-right anchor
} PictureMode;

// Renders a textured quad using the bindless texture array.
void Vk_drawTexture(void *cmdBuffer, float surfaceW, float surfaceH,
                    float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    int32_t textureId,
                    PictureMode mode,
                    float imgW, float imgH);

// Renders an SDF text glyph using the bindless texture array.
void Vk_drawSDFText(void *cmdBuffer, float surfaceW, float surfaceH,
                    float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    int32_t textureId, float bold, float smoothness,
                    float u0, float v0, float u1, float v1);

// Renders a color glyph (runtime/baked emoji) from a color atlas page.
// Same quad/UV plumbing as Vk_drawSDFText, but the fragment branch outputs
// raw RGBA: tint is ignored except for alpha (label opacity).
void Vk_drawColorGlyph(void *cmdBuffer, float surfaceW, float surfaceH,
                       float x, float y, float w, float h, float alpha,
                       int32_t textureId,
                       float u0, float v0, float u1, float v1);

#endif // VULKAN_VK_H
