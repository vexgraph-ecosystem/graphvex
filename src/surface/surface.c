#include "surface/surface.h"
#include <stdlib.h>
#include <string.h>
#include "annotation/definition.h"
#include "annotation/overview.h"

#if defined(__APPLE__)
extern void *Surface_cocoaCreateIOSurface(uint32_t width, uint32_t height);
extern void  Surface_cocoaFreeIOSurface(void *ioSurface);
extern void *Surface_cocoaAttach(void *parentView, void *ioSurface, float scale);
extern void  Surface_cocoaDetach(void *caLayer);
extern void  Surface_cocoaSync(void *caLayer, int ptW, int ptH, float scale);
extern void  Surface_cocoaPresent(void *caLayer);
extern void *Surface_cocoaCreateMetalTexture(void *ioSurface, void *mtlDevice);
extern void  Surface_cocoaFreeMetalTexture(void *metalTexture);
#endif

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Surface
 * ============================================================================
 * Zero-swapchain hardware display surface backed by IOSurface on Apple Silicon.
 * Bypasses traditional multi-buffered swapchain churn during window resize by
 * allocating a single display-maximum surface and presenting via CoreAnimation
 * with kCAGravityTopLeft. Live-resize steps only update the scissor rectangle.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Surface (surface/surface.c)
 * LEVEL: L3 — Module Code (graphvex hardware display surface)
 * ============================================================================
 * SUMMARY:
 *   Manages an IOSurface and its presentation CALayer. Imports into Vulkan
 *   or Metal for zero-copy rendering without swapchains.
 *
 * STRUCT FIELDS (Mirroring surface/surface.h):
 * ----------------------------------------------------------------------------
 *   void *ioSurface;        // Underlying IOSurfaceRef (macOS)
 *   void *caLayer;          // Presentation CALayer
 *   void *parentView;       // Attached host NSView
 *   void *vkImage;          // Imported VkImage handle (if Vulkan active)
 *   void *metalTexture;     // id<MTLTexture> handle (if Metal active)
 *   uint32_t width;         // Allocated hardware width (pixels)
 *   uint32_t height;        // Allocated hardware height (pixels)
 *   uint32_t activeWidth;   // Current active live render width (pixels)
 *   uint32_t activeHeight;  // Current active live render height (pixels)
 *   float scale;            // Display backing scale factor (1.0 or 2.0)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Surface_0(void)
 *   - Surface_2(maxWidth, maxHeight)
 *
 * Public Core Functions: (.h)
 *   - Surface_free(surface)
 *   - Surface_attach(surface, nativeView)
 *   - Surface_detach(surface)
 *   - Surface_sync(surface, pointWidth, pointHeight, scale)
 *   - Surface_setActiveArea(surface, activeW, activeH)
 *   - Surface_present(surface)
 *
 * Public Getters: (.h)
 *   - Surface_getIOSurface(surface)
 *   - Surface_getLayer(surface)
 *   - Surface_getVkImage(surface)
 *   - Surface_getMetalTexture(surface)
 *   - Surface_getWidth(surface)
 *   - Surface_getHeight(surface)
 *   - Surface_getActiveWidth(surface)
 *   - Surface_getActiveHeight(surface)
 * ============================================================================
 */

Surface *Surface_0(void) {
    // Default to 4K monitor maximum footprint
    return Surface_2(3840, 2160);
}

Surface *Surface_2(uint32_t maxWidth, uint32_t maxHeight) {
    if (maxWidth == 0 || maxHeight == 0)
        return nullptr;

    Surface *surface = (Surface*) calloc(1, sizeof(Surface));
    if (surface == nullptr)
        return nullptr;

    (*surface).width = maxWidth;
    (*surface).height = maxHeight;
    (*surface).activeWidth = maxWidth;
    (*surface).activeHeight = maxHeight;
    (*surface).scale = 1.0f;

#if defined(__APPLE__)
    (*surface).ioSurface = Surface_cocoaCreateIOSurface(maxWidth, maxHeight);
    if ((*surface).ioSurface == nullptr) {
        free(surface);
        return nullptr;
    }
    (*surface).metalTexture = Surface_cocoaCreateMetalTexture((*surface).ioSurface, nullptr);
#endif

    return surface;
}

void Surface_free(Surface *surface) {
    if (surface == nullptr)
        return;

    Surface_detach(surface);

#if defined(__APPLE__)
    if ((*surface).metalTexture != nullptr) {
        Surface_cocoaFreeMetalTexture((*surface).metalTexture);
        (*surface).metalTexture = nullptr;
    }
    if ((*surface).ioSurface != nullptr) {
        Surface_cocoaFreeIOSurface((*surface).ioSurface);
        (*surface).ioSurface = nullptr;
    }
#endif

    free(surface);
}

bool Surface_attach(Surface *surface, void *nativeView) {
    if (surface == nullptr || nativeView == nullptr)
        return false;

    if ((*surface).caLayer != nullptr)
        Surface_detach(surface);

#if defined(__APPLE__)
    (*surface).parentView = nativeView;
    (*surface).caLayer = Surface_cocoaAttach(nativeView, (*surface).ioSurface, (*surface).scale);
    return (*surface).caLayer != nullptr;
#else
    (*surface).parentView = nativeView;
    return true;
#endif
}

void Surface_detach(Surface *surface) {
    if (surface == nullptr || (*surface).caLayer == nullptr)
        return;

#if defined(__APPLE__)
    Surface_cocoaDetach((*surface).caLayer);
#endif
    (*surface).caLayer = nullptr;
    (*surface).parentView = nullptr;
}

void Surface_sync(Surface *surface, int pointWidth, int pointHeight, float scale) {
    if (surface == nullptr || pointWidth <= 0 || pointHeight <= 0)
        return;

    (*surface).scale = (scale > 0.0f) ? scale : 1.0f;
    uint32_t pxW = (uint32_t) ((float) pointWidth * (*surface).scale);
    uint32_t pxH = (uint32_t) ((float) pointHeight * (*surface).scale);

    Surface_setActiveArea(surface, pxW, pxH);

#if defined(__APPLE__)
    if ((*surface).caLayer != nullptr)
        Surface_cocoaSync((*surface).caLayer, pointWidth, pointHeight, (*surface).scale);
#endif
}

void Surface_setActiveArea(Surface *surface, uint32_t activeW, uint32_t activeH) {
    if (surface == nullptr)
        return;

    (*surface).activeWidth = (activeW > (*surface).width) ? (*surface).width : activeW;
    (*surface).activeHeight = (activeH > (*surface).height) ? (*surface).height : activeH;
}

void Surface_present(Surface *surface) {
    if (surface == nullptr)
        return;

#if defined(__APPLE__)
    if ((*surface).caLayer != nullptr)
        Surface_cocoaPresent((*surface).caLayer);
#endif
}

void *Surface_getIOSurface(const Surface *surface) {
    if (surface == nullptr)
        return nullptr;
    return (*surface).ioSurface;
}

void *Surface_getLayer(const Surface *surface) {
    if (surface == nullptr)
        return nullptr;
    return (*surface).caLayer;
}

void *Surface_getVkImage(const Surface *surface) {
    if (surface == nullptr)
        return nullptr;
    return (*surface).vkImage;
}

void *Surface_getMetalTexture(const Surface *surface) {
    if (surface == nullptr)
        return nullptr;
    return (*surface).metalTexture;
}

uint32_t Surface_getWidth(const Surface *surface) {
    if (surface == nullptr)
        return 0;
    return (*surface).width;
}

uint32_t Surface_getHeight(const Surface *surface) {
    if (surface == nullptr)
        return 0;
    return (*surface).height;
}

uint32_t Surface_getActiveWidth(const Surface *surface) {
    if (surface == nullptr)
        return 0;
    return (*surface).activeWidth;
}

uint32_t Surface_getActiveHeight(const Surface *surface) {
    if (surface == nullptr)
        return 0;
    return (*surface).activeHeight;
}
