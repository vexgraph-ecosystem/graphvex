#ifndef RASTER_RASTER_GRAPHICS_H
#define RASTER_RASTER_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/graphics.h"
#include "lang/image.h"

// raster/raster_graphics.h — RasterGraphics: the software backend row.
//
// Implements every Graphics verb in CPU code into an owned Image (the RGBA8
// framebuffer): the software row of the unified seam
// (Graphics_setGraphics(LANG_BACKEND_RASTER)). One process-global framebuffer
// stands behind the row — the row's impls read file-local state, so the Graphics
// table carries no self pointer (the Pixel Coordinate Contract: all geometry is
// native pixels; a software backend never sees NDC).
//
// Lifecycle: the framebuffer is created/rebound by RasterGraphics_resize (cold
// path). Before that, every verb cold-returns false. The framebuffer is heap-owned
// by the row and freed by RasterGraphics_shutdown (the Teardown Order Law).

const Graphics *RasterGraphics_getRow(void);

// Bind/rebind the drawable extent: (re)allocates the RGBA8 framebuffer.
// Returns false on zero dims or OOM.
bool RasterGraphics_resize(uint32_t width, uint32_t height);

// Swap the raster target to a caller-owned Image (cold path only — a bake
// sub-pass). No allocation, no free: the previous framebuffer stays live with
// the caller (restore via RasterGraphics_resize). Returns false on hostiles
// (null image, zero dims, dims not matching the image extent).
bool RasterGraphics_setFramebuffer(Image *fb, uint32_t width, uint32_t height);

// Free the owned framebuffer (the Teardown Order Law). Null-safe.
void RasterGraphics_shutdown(void);

// --- Getters (null-safe) ---
Image   *RasterGraphics_getFramebuffer(void);
uint32_t RasterGraphics_getWidth(void);
uint32_t RasterGraphics_getHeight(void);
bool     RasterGraphics_isReady(void);

#endif // RASTER_RASTER_GRAPHICS_H
