#ifndef RASTER_RASTER_GRAPHICS_H
#define RASTER_RASTER_GRAPHICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buffer/buffer.h"
#include "c23/constructor.h"
#include "graphics/graphics.h"
#include "../graphics/type.h"

// raster/raster_graphics.h — RasterGraphics: the software backend row.
//
// Single Class Per File Law: RasterGraphics.
//
// RasterGraphics implements every Graphics verb in CPU code into an owned
// RGBA8 buffer: the DGraphics row of the unified seam (Graphics_setGraphics
// with GRAPHICS_BACKEND_RASTER). One process-global singleton stands behind
// the row — the row's impls read file-local state, so the Graphics table
// carries no self pointer (the Pixel Coordinate Contract: all geometry is
// native hardware pixels; NDC exists only inside vertex shaders — a
// software backend never sees NDC).
//
// Lifecycle: the framebuffer is created/rebound by RasterGraphics_resize
// on window attach (cold path). Before that, every verb cold-returns false.
// The framebuffer is arena-backed (Buffer_4), freed wholesale by the arena
// at teardown (the Teardown Order Law).

typedef struct RasterGraphics {
    Buffer *framebuffer;  // RGBA8 native-px software framebuffer (arena-backed); null until resize; swap-able via setFramebuffer
    uint32_t width;       // drawable extent, native px; 0 until resize
    uint32_t height;      // drawable extent, native px; 0 until resize
    bool clipEnabled;     // scissor active (Graphics_clip passed non-NULL)
    float clipX;          // scissor top-left x, native px
    float clipY;          // scissor top-left y, native px
    float clipW;          // scissor extent w, native px
    float clipH;          // scissor extent h, native px
} RasterGraphics;

// The row: pass to Graphics_setGraphics via GRAPHICS_BACKEND_RASTER.
const Graphics *RasterGraphics_getRow(void);

// Bind/rebind the drawable extent (cold path, window attach/resize).
// Allocates the arena-backed RGBA8 framebuffer. Returns false on
// zero dims or OOM.
bool RasterGraphics_resize(uint32_t width, uint32_t height);

// SWAP the raster target to a caller-owned buffer (cold path only — the
// retained-subtree bake sub-pass). No allocation, no free: the previous
// framebuffer stays live with the caller (restore via
// RasterGraphics_setFramebuffer(old, oldW, oldH) after rendering the
// sub-pass). Clip state is NOT touched by the swap — the caller owns the
// full pass bookkeeping around it. Returns false on hostiles (null fb,
// zero dims, dims that do not match the buffer's own extent, or a
// non-4-channel buffer) with state untouched.
bool RasterGraphics_setFramebuffer(Buffer *fb, uint32_t width, uint32_t height);

// Null-safe inspectors (null/unset framebuffer yields 0 / NULL):
Buffer *RasterGraphics_getFramebuffer(void);
uint32_t RasterGraphics_getWidth(void);
uint32_t RasterGraphics_getHeight(void);
bool RasterGraphics_isReady(void);

#endif