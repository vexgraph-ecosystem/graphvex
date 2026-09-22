#include "vulkan/vk_window_seam.h"
#include <stdio.h>
#include <stddef.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VkWindowSeam
 * ============================================================================
 * Inversion-of-control mediation bridge linking the graphics subsystem (graphvex)
 * with the underlying platform operating system window manager (hotcwap) without
 * introducing cross-repository dependency entanglements.
 *
 * Exposes a clean decoupled function-pointer dispatch interface through which
 * the host application injects platform window hooks (such as CAMetalLayer
 * extraction, display pacing modes, live resize detection, and occlusion gating)
 * prior to graphics device initialization. Graphvex invokes these registered
 * accessors defensively with zero direct structural access to window-system
 * memory, preventing abstraction leakage across repository boundaries.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VkWindowSeam (vulkan/vk_window_seam.c)
 * LEVEL: L1 — Pure Glue (Window system inversion-of-control seam)
 * ============================================================================
 * SUMMARY:
 *   Window-system seam providing decoupled callback dispatch between graphvex
 *   and platform window implementations. All interactions occur via opaque void
 *   handles and registered callback function pointers.
 *
 * STRUCT FIELDS:
 * ----------------------------------------------------------------------------
 *   s_window;                // opaque window pointer
 *   s_metalLayerFn;          // CAMetalLayer extractor callback
 *   s_isTransparentFn;       // transparency query callback
 *   s_getPresentModeFn;      // presentation mode query callback
 *   s_renderGenerationFn;    // render generation query callback
 *   s_isLiveResizingFn;      // live resize query callback
 *   s_isMinimizedFn;         // minimization query callback
 *   s_setResizeRenderHookFn; // resize render hook callback
 *   s_setGravityTopLeftFn;   // layer gravity setter callback
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - (none)
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Vk_setWindowSeam(...)                              : Register platform window callback table
 *   - Vk_seamSetResizeRenderHook(fn, userdata)           : Install resize rendering hook on window
 *   - Vk_seamSetGravityTopLeft(void)                     : Set window layer anchor gravity
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - (none)
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Vk_seamWindow(void)                                : Query opaque platform window handle
 *   - Vk_seamMetalLayer(void)                            : Query window CAMetalLayer handle
 *   - Vk_seamIsTransparent(void)                         : Query window transparency state
 *   - Vk_seamGetPresentMode(void)                        : Query window presentation pacing mode
 *   - Vk_seamRenderGeneration(void)                      : Query window render generation counter
 *   - Vk_seamIsLiveResizing(void)                        : Probe live interactive resizing status
 *   - Vk_seamIsMinimized(void)                           : Probe window occlusion or miniaturization
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

static void *s_window = nullptr;
static void *(*s_metalLayerFn)(void *w) = nullptr;
static bool (*s_isTransparentFn)(void *w) = nullptr;
static VkWindowPresentMode (*s_getPresentModeFn)(void *w) = nullptr;
static uint64_t (*s_renderGenerationFn)(void *w) = nullptr;
static bool (*s_isLiveResizingFn)(void *w) = nullptr;
static bool (*s_isMinimizedFn)(void *w) = nullptr;
static void (*s_setResizeRenderHookFn)(void *w, void *fn, void *userdata) = nullptr;
static void (*s_setGravityTopLeftFn)(void *w) = nullptr;

// CONSTRUCTORS (PUBLIC & PRIVATE)

// (none)

// CORE FUNCTIONS (PUBLIC & PRIVATE)

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
) {
    s_window = window;
    s_metalLayerFn = metalLayerFn;
    s_isTransparentFn = isTransparentFn;
    s_getPresentModeFn = getPresentModeFn;
    s_renderGenerationFn = renderGenerationFn;
    s_isLiveResizingFn = isLiveResizingFn;
    s_setResizeRenderHookFn = setResizeRenderHookFn;
    s_setGravityTopLeftFn = setGravityTopLeftFn;
    s_isMinimizedFn = isMinimizedFn;
    if (window)
        fprintf(stderr, "vk: window seam installed\n");
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

// SETTERS (PUBLIC & PRIVATE)

// (none)

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
void *Vk_seamWindow(void) {
    return s_window;
}

;;GETTER
void *Vk_seamMetalLayer(void) {
    if (!s_window || !s_metalLayerFn)
        return nullptr;
    return s_metalLayerFn(s_window);
}

;;GETTER
bool Vk_seamIsTransparent(void) {
    if (!s_window || !s_isTransparentFn)
        return false;
    return s_isTransparentFn(s_window);
}

;;GETTER
VkWindowPresentMode Vk_seamGetPresentMode(void) {
    if (!s_window || !s_getPresentModeFn)
        return VK_WINDOW_PRESENT_FIFO;
    return s_getPresentModeFn(s_window);
}

;;GETTER
uint64_t Vk_seamRenderGeneration(void) {
    if (!s_window || !s_renderGenerationFn)
        return 0;
    return s_renderGenerationFn(s_window);
}

;;GETTER
bool Vk_seamIsLiveResizing(void) {
    if (!s_window || !s_isLiveResizingFn)
        return false;
    return s_isLiveResizingFn(s_window);
}

;;GETTER
bool Vk_seamIsMinimized(void) {
    if (!s_window || !s_isMinimizedFn)
        return false;
    return s_isMinimizedFn(s_window);
}
