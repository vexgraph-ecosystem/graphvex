#ifndef SURFACE_SURFACE_H
#define SURFACE_SURFACE_H

#include <stdbool.h>
#include <stdint.h>
#include "c23/constructor.h"
#include "graphvex/type.h"

// surface/surface.h — Zero-swapchain IOSurface presentation canvas.
//
// Single Class Per File Law: Surface.
//
// Replaces VkSwapchainKHR with an OS-managed hardware surface (IOSurface
// on macOS). The master surface is allocated once at monitor maximum, and
// the on-screen window is a scissored viewport crop of it.
//
// Presentation bypasses swapchains: the IOSurface is bound directly as a
// CALayer's contents with kCAGravityTopLeft. Live resizes never rebuild
// swapchains; they simply update the scissor rect and layer frame.

typedef struct Surface {
    void *ioSurface;        // Underlying IOSurfaceRef (macOS)
    void *caLayer;          // Presentation CALayer
    void *parentView;       // Attached host NSView
    void *vkImage;          // Imported VkImage handle (if Vulkan active)
    void *metalTexture;     // id<MTLTexture> handle (if Metal active)
    uint32_t width;         // Allocated hardware width (pixels)
    uint32_t height;        // Allocated hardware height (pixels)
    uint32_t activeWidth;   // Current active live render width (pixels)
    uint32_t activeHeight;  // Current active live render height (pixels)
    float scale;            // Display backing scale factor (1.0 or 2.0)
} Surface;

// Constructors
Surface *Surface_0(void);
Surface *Surface_2(uint32_t maxWidth, uint32_t maxHeight);

#define Surface(...) CONSTRUCTOR_DISPATCH(Surface, __VA_ARGS__)

// Lifecycle
void Surface_free(Surface *surface);
bool Surface_attach(Surface *surface, void *nativeView);
void Surface_detach(Surface *surface);

// Geometry & Live Viewport Sync
void Surface_sync(Surface *surface, int pointWidth, int pointHeight, float scale);
void Surface_setActiveArea(Surface *surface, uint32_t activeW, uint32_t activeH);

// Presentation
void Surface_present(Surface *surface);

// Native Resource Accessors
void *Surface_getIOSurface(const Surface *surface);
void *Surface_getLayer(const Surface *surface);
void *Surface_getVkImage(const Surface *surface);
void *Surface_getMetalTexture(const Surface *surface);

uint32_t Surface_getWidth(const Surface *surface);
uint32_t Surface_getHeight(const Surface *surface);
uint32_t Surface_getActiveWidth(const Surface *surface);
uint32_t Surface_getActiveHeight(const Surface *surface);

#endif
