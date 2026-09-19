#ifndef DIRECT_DIRECT_GRAPHICS_H
#define DIRECT_DIRECT_GRAPHICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "buffer/buffer.h"
#include "c23/constructor.h"
#include "graphics/graphics.h"
#include "graphvex/type.h"

// direct/direct_graphics.h — DirectGraphics: the software backend row.
//
// Single Class Per File Law: DirectGraphics.
//
// DirectGraphics implements every Graphics verb in CPU code into an owned
// RGBA8 buffer: the DGraphics row of the unified seam (Graphics_setGraphics
// with GRAPHICS_BACKEND_DIRECT). One process-global singleton stands behind
// the row — the row's impls read file-local state, so the Graphics table
// carries no self pointer (the Pixel Coordinate Contract: all geometry is
// native hardware pixels; NDC exists only inside vertex shaders — a
// software backend never sees NDC).
//
// Lifecycle: the framebuffer is created/rebound by DirectGraphics_resize
// on window attach (cold path). Before that, every verb cold-returns false.
// The framebuffer is arena-backed (Buffer_4), freed wholesale by the arena
// at teardown (the Teardown Order Law).

typedef struct DirectGraphics {
    Buffer *framebuffer;  // RGBA8 native-px software framebuffer (arena-backed); null until resize
    uint32_t width;       // drawable extent, native px; 0 until resize
    uint32_t height;      // drawable extent, native px; 0 until resize
    bool clipEnabled;     // scissor active (Graphics_clip passed non-NULL)
    float clipX;          // scissor top-left x, native px
    float clipY;          // scissor top-left y, native px
    float clipW;          // scissor extent w, native px
    float clipH;          // scissor extent h, native px
} DirectGraphics;

// The row: pass to Graphics_setGraphics via GRAPHICS_BACKEND_DIRECT.
const Graphics *DirectGraphics_getRow(void);

// Bind/rebind the drawable extent (cold path, window attach/resize).
// Allocates the arena-backed RGBA8 framebuffer. Returns false on
// zero dims or OOM.
bool DirectGraphics_resize(uint32_t width, uint32_t height);

// Null-safe inspectors (null/unset framebuffer yields 0 / NULL):
Buffer *DirectGraphics_getFramebuffer(void);
uint32_t DirectGraphics_getWidth(void);
uint32_t DirectGraphics_getHeight(void);
bool DirectGraphics_isReady(void);

#endif