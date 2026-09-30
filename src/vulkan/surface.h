#ifndef GRAPHICS_SURFACE_H
#define GRAPHICS_SURFACE_H

#include <stdbool.h>
#include <stdint.h>

#include "image.h"

// graphvex R3 — vulkan/surface.h
//
// *** NO SWAPCHAIN. *** A Surface is a host-borrowed native destination
// (a CAMetalLayer on Apple, HWND on Windows, xcb window on Linux) plus ONE
// retained present Image that we render into. Surface_present() hands the
// completed image to the host seam (the Frame blits a Metal drawable). We never
// create, acquire, or present a VkSwapchainKHR — there is no such call in this
// codebase (the Single-Seam Canvas Law).

typedef struct Surface Surface;

Surface *Surface_0(void);
Surface *Surface_2(void *native, uint32_t width, uint32_t height);
void Surface_destroy(Surface *surface);

bool Surface_resize(Surface *surface, uint32_t width, uint32_t height);
uint32_t Surface_width(const Surface *surface);    // native px
uint32_t Surface_height(const Surface *surface);
bool Surface_isValid(const Surface *surface);
void *Surface_handle(const Surface *surface);      // borrowed native destination
Image *Surface_presentImage(Surface *surface);     // retained target to render into

// Hand the completed present image to the host seam. False until the platform
// blit lands (CAMetalLayer on Apple, WSI-free copy elsewhere) or when the
// device is lost. Never a swapchain present.
bool Surface_present(Surface *surface);

#endif // GRAPHICS_SURFACE_H
