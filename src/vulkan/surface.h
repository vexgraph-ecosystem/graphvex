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

// The host present seam. graphvex owns no platform code and never includes
// Metal/CoreAnimation, so the layer that owns the borrowed native destination
// (an R1 window / R4 frame) installs a blit that copies the retained present
// Image into it — a CAMetalLayer contents update on Apple. The callback reads
// Surface_handle and Surface_presentImage, writes the destination, and returns
// true only when a frame was actually handed over.
typedef bool (*SurfacePresentFn)(Surface *surface, void *userdata);

Surface *Surface_0(void);
Surface *Surface_2(void *native, uint32_t width, uint32_t height);
void Surface_destroy(Surface *surface);

bool Surface_resize(Surface *surface, uint32_t width, uint32_t height);
uint32_t Surface_width(const Surface *surface);    // native px
uint32_t Surface_height(const Surface *surface);
bool Surface_isValid(const Surface *surface);
void *Surface_handle(const Surface *surface);      // borrowed native destination
Image *Surface_presentImage(Surface *surface);     // retained target to render into

// Register the host blit. The callback + userdata are borrowed, never owned;
// passing NULL clears the seam. Idempotent (re-register replaces).
void Surface_onPresent(Surface *surface, SurfacePresentFn fn, void *userdata);

// Hand the completed present image to the host seam: invoke the registered
// blit. Returns false when no seam is installed (an offscreen surface has
// nowhere to present) or when the blit reports failure. Never a swapchain
// present — the host owns the drawable.
bool Surface_present(Surface *surface);

#endif // GRAPHICS_SURFACE_H
