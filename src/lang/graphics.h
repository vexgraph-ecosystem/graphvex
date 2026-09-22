#ifndef LANG_GRAPHICS_H
#define LANG_GRAPHICS_H

#include <stdbool.h>
#include <stdint.h>

#include "lang/brush.h"
#include "lang/device.h"     // LANG_BACKEND_* (the row self-reports its dialect id)
#include "lang/image.h"
#include "lang/stroke.h"
#include "lang/rect/rectangle.h" // vexspoke R2 Rectangle (native pixels, Y-down)

// lang/graphics.h — the unified Graphics seam (all drawable verbs).
//
// ONE table carries the whole backend contract as a function-pointer row: frame
// lifecycle, clear/clip state, and every drawable verb. Backends (RasterGraphics,
// VkGraphics, MetalGraphics, ...) are their own classes; each exports ONE
// `const Graphics *` row and registers it. `Graphics_setGraphics(backendId)`
// stamps the row into the current context, and the `Graphics_<verb>` forwarders
// call through it — so callers switch renderers with one line and never touch a
// backend type (the Pixel Coordinate Contract: every verb takes native hardware
// pixels; NDC exists only inside vertex shaders).
//
//   Graphics_setGraphics(LANG_BACKEND_RASTER);
//   Graphics_resize(wpx, hpx);
//   Graphics_fillRect(&rect, brush);   // -> (*current).fillRect(&rect, brush)
//
// This is the UI's drawing vocabulary: a GraphicsComponent's rect (backgroundColor,
// borderColor, radius) is painted through these verbs into an Image.

typedef struct Shape Shape;   // vector paths — forward-declared; the verbs cold-false until Shape lands.

typedef struct Graphics {
    uint32_t backendId;       // LANG_BACKEND_* (self-reported by the row)
    // Frame lifecycle — begin/end/present bracket a frame.
    bool (*begin)(void);
    bool (*end)(void);
    bool (*present)(void);
    bool (*resize)(uint32_t width, uint32_t height);  // native px, cold path
    // State
    bool (*clear)(uint32_t color);                    // 0xRRGGBBAA, full drawable
    bool (*clip)(const Rectangle *rect);              // scissor; null = reset
    // Verbs — native pixels; each compiles to 1..N quads in a GPU backend.
    bool (*fillRect)(const Rectangle *rect, const Brush *brush);
    bool (*drawRect)(const Rectangle *rect, const Stroke *stroke);
    bool (*fillCircle)(float cx, float cy, float radius, const Brush *brush);
    bool (*drawCircle)(float cx, float cy, float radius, const Stroke *stroke);
    bool (*fillPath)(const Shape *shape, const Brush *brush);
    bool (*drawPath)(const Shape *shape, const Stroke *stroke);
    bool (*drawText)(const Rectangle *rect, const char *text, const Brush *brush);
    bool (*drawImage)(const Image *image, const Rectangle *dst); // dst comes last
} Graphics;

// Backend rows (one class pair per backend). Only the raster row ships today;
// the GPU rows land with their dialects.
const Graphics *RasterGraphics_getRow(void);

// --- Core functions ---
// Register a backend row. Idempotent per backendId (re-register replaces). The
// row must outlive the process (backends export static rows). Returns false on
// null row, null verbs, or backendId NONE.
bool Graphics_registerRow(const Graphics *row);

// Stamp the row for backendId into the current context. Returns false on an
// unknown id; on failure the previously selected row stays active.
bool Graphics_setGraphics(uint32_t backendId);

// The active row, or a null row (all verbs cold-false) when none is selected.
const Graphics *Graphics_getCurrent(void);
uint32_t Graphics_getGraphicsId(void);   // LANG_BACKEND_NONE until selected

// --- Forwarders — call through the current row; false when unselected ---
bool Graphics_begin(void);
bool Graphics_end(void);
bool Graphics_present(void);
bool Graphics_resize(uint32_t width, uint32_t height);
bool Graphics_clear(uint32_t color);
bool Graphics_clip(const Rectangle *rect);
bool Graphics_fillRect(const Rectangle *rect, const Brush *brush);
bool Graphics_drawRect(const Rectangle *rect, const Stroke *stroke);
bool Graphics_fillCircle(float cx, float cy, float radius, const Brush *brush);
bool Graphics_drawCircle(float cx, float cy, float radius, const Stroke *stroke);
bool Graphics_fillPath(const Shape *shape, const Brush *brush);
bool Graphics_drawPath(const Shape *shape, const Stroke *stroke);
bool Graphics_drawImage(const Image *image, const Rectangle *dst);
bool Graphics_drawText(const Rectangle *rect, const char *text, const Brush *brush);

#endif // LANG_GRAPHICS_H
