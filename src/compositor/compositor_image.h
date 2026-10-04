#ifndef GRAPHVEX_COMPOSITOR_IMAGE_H
#define GRAPHVEX_COMPOSITOR_IMAGE_H

#include "compositor/compositor.h"
#include "image.h"

/* Explicit CPU boundary: Image is interpreted as straight-alpha sRGB RGBA8.
 * Import borrows a populated CPU shadow, never a native GPU handle. BGRA and
 * other formats return UNSUPPORTED. No resize, fit, or implicit shadow fill.
 * Export owns a new RGBA8 Image: unpremultiply, encode sRGB, clamp to [0,1],
 * round to nearest byte. Alpha zero exports canonical transparent black.
 * Origin remains in the surface: returned Image is only its local pixel array;
 * submit it at CompositorSurface_bounds(surface).x/y. Empty export rejects.
 * Out unchanged on failure; caller owns/free success via the matching destroy.
 * Same external synchronization/live pointer contract as compositor.h. */
CompositorStatus CompositorSurface_fromImage(const Image *image,
                                             int32_t x, int32_t y,
                                             CompositorSurface **out);
CompositorStatus CompositorSurface_toImage(const CompositorSurface *surface,
                                           Image **out);

#endif
