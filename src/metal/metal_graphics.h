#ifndef METAL_METAL_GRAPHICS_H
#define METAL_METAL_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "graphics/graphics.h"
#include "graphvex/type.h"

// metal/metal_graphics.h — MetalGraphics: the Metal backend row (;;INCOMPLETE).
//
// Single Class Per File Law: MetalGraphics.
//
// The Metal row of the unified Graphics seam. Secondary backend: macOS
// renders Vulkan through MoltenVK, so Metal is for when the engine takes
// shape and a native-CAMetalLayer path matters. Until then the row is a
// cold-false stub: registration + selection work (setGraphics with
// GRAPHICS_BACKEND_METAL succeeds once MetalGraphics_0 runs) and every
// verb/device call returns false.
//
// The struct mirrors VkGraphics field-for-field so the Metal
// implementation can take shape without an API break; when it does, this
// file becomes metal_graphics.m with CAMetalLayer wiring (the window owns
// the layer, graphvex only renders into it per the Vertical Integration
// Law).
//
// Lifecycle: one file-local process-global value, zero steady-state
// allocation. Registration via MetalGraphics_0 (idempotent).

typedef struct MetalGraphics {
    uint32_t width;       // newest native-px drawable extent; always 0 (stub)
    uint32_t height;      // newest native-px drawable extent
    uint32_t clearColor;  // staged 0xAARRGGBB; always 0 (stub)
    bool clearPending;    // unused (stub)
    bool frameOpen;       // unused (stub)
} MetalGraphics;

// The row: pass to Graphics_setGraphics via GRAPHICS_BACKEND_METAL.
// NULL until MetalGraphics_0 registers the process-global singleton.
const Graphics *MetalGraphics_getRow(void);

// Register the process-global singleton (idempotent). Returns the
// singleton (never null).
MetalGraphics *MetalGraphics_0(void);

// Null-safe inspectors (before registration yields 0 / false):
uint32_t MetalGraphics_getWidth(void);
uint32_t MetalGraphics_getHeight(void);
uint32_t MetalGraphics_getClearColor(void);
bool MetalGraphics_isClearPending(void);
bool MetalGraphics_isFrameOpen(void);
bool MetalGraphics_isReady(void);

#endif