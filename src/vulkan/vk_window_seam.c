#include "vulkan/vk_window_seam.h"
#include <stdio.h>
#include <stddef.h> // nullptr in C23

// vulkan/vk_window_seam.c — the Window-system seam glue.
//
// graphvex (the graphics foundation layer) must NEVER include headers from
// hotcwap, darling, or api-haven (Rule 17). But Vk_init / presentFrameLocked
// need to interact with the OS window — for surface creation, pacing, resize
// detection, etc. So the host (hotcwap) registers opaque callbacks here
// before calling Vk_init, and graphvex calls through them via the Vk_seam*
// accessors.
//
// This file is the ONLY bridge between graphvex and the Window type.
// All handles are void* — graphvex never touches the real Window struct.

static void *s_window = nullptr;
static void *(*s_metalLayerFn)(void *w) = nullptr;
static bool (*s_isTransparentFn)(void *w) = nullptr;
static VkWindowPresentMode (*s_getPresentModeFn)(void *w) = nullptr;
static uint64_t (*s_renderGenerationFn)(void *w) = nullptr;
static bool (*s_isLiveResizingFn)(void *w) = nullptr;
static void (*s_setResizeRenderHookFn)(void *w, void *fn, void *userdata) = nullptr;
static void (*s_setGravityTopLeftFn)(void *w) = nullptr;

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
) {
    s_window = window;
    s_metalLayerFn = metalLayerFn;
    s_isTransparentFn = isTransparentFn;
    s_getPresentModeFn = getPresentModeFn;
    s_renderGenerationFn = renderGenerationFn;
    s_isLiveResizingFn = isLiveResizingFn;
    s_setResizeRenderHookFn = setResizeRenderHookFn;
    s_setGravityTopLeftFn = setGravityTopLeftFn;
    if (window)
        fprintf(stderr, "vk: window seam installed\n");
}

// Accessor for the installed window handle.
void *Vk_seamWindow(void) {
    return s_window;
}

// Return the window's CAMetalLayer (for surface creation).
void *Vk_seamMetalLayer(void) {
    if (!s_window || !s_metalLayerFn)
        return nullptr;
    return s_metalLayerFn(s_window);
}

// Seam accessors — delegate to installed callbacks, return defaults if absent.
bool Vk_seamIsTransparent(void) {
    if (!s_window || !s_isTransparentFn)
        return false;
    return s_isTransparentFn(s_window);
}

VkWindowPresentMode Vk_seamGetPresentMode(void) {
    if (!s_window || !s_getPresentModeFn)
        return VK_WINDOW_PRESENT_FIFO;
    return s_getPresentModeFn(s_window);
}

uint64_t Vk_seamRenderGeneration(void) {
    if (!s_window || !s_renderGenerationFn)
        return 0;
    return s_renderGenerationFn(s_window);
}

bool Vk_seamIsLiveResizing(void) {
    if (!s_window || !s_isLiveResizingFn)
        return false;
    return s_isLiveResizingFn(s_window);
}

void Vk_seamSetResizeRenderHook(void *fn, void *userdata) {
    if (!s_window || !s_setResizeRenderHookFn)
        return;
    s_setResizeRenderHookFn(s_window, fn, userdata);
}

void Vk_seamSetGravityTopLeft(void) {
    if (!s_window || !s_setGravityTopLeftFn)
        return;
    s_setGravityTopLeftFn(s_window);
}
