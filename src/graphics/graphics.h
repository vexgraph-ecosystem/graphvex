#ifndef GRAPHICS_GRAPHICS_H
#define GRAPHICS_GRAPHICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "graphvex/type.h"
#include "image/image.h"
#include "lang/rect/rectangle.h"
#include "paint/brush.h"
#include "paint/stroke.h"
#include "vector/shape.h"

// graphics/graphics.h — the unified Graphics seam (all drawable verbs).
//
// Single Class Per File Law: Graphics.
//
// One Graphics struct carries the WHOLE backend contract as a function
// pointer table: frame lifecycle, clear/clip state, and every drawable
// verb. Backends (VkGraphics, MetalGraphics, DirectGraphics) are their
// own classes; each exports ONE const row via *_getRow(). A single
// selection call stamps a row into graphics.c's `currentGraphics`
// context, and the Graphics_* forwarders below call through it — so
// callers switch renderers with one line and never touch backend
// specifics (the Pixel Coordinate Contract: every verb takes native
// hardware pixels; -1..1 NDC exists only inside vertex shaders).
//
//   Graphics_setGraphics(GRAPHICS_BACKEND_DIRECT);
//   Graphics_resize(winWpx, winHpx);
//   Graphics_fillRect(rect, brush);   // -> (*currentGraphics).fillRect(rect, brush)
//
// Layout:
//   - Graphics_setGraphics(backendId)   : copy row into currentGraphics
//   - Graphics_getGraphicsId()          : 0 = none selected yet
//   - Graphics_getCurrent()             : the active row (never null)
//   - Graphics_<verb>(...)              : null-guarded forwarders
//
// A native-pixel drawable extent is bound through Graphics_resize on
// window attach (cold path); the per-frame drawable/frame plumbing is
// the GraphicsDrawable/GraphicsFrame sketch in graphvex/device.h.

// Backend selection ids (not bit flags). The canonical ids — device.h
// and all backends reference these, never re-declare them.
#define GRAPHICS_BACKEND_VULKAN 1u
#define GRAPHICS_BACKEND_METAL  2u
#define GRAPHICS_BACKEND_DIRECT 3u

typedef struct Graphics {
    uint32_t backendId;       // GRAPHICS_BACKEND_* (self-reported by the row)
    // Frame lifecycle — one active context; begin/end/present bracket a frame.
    bool (*begin)(void);                          // start recording a frame
    bool (*end)(void);                            // flush/submit the frame
    bool (*present)(void);                        // swap/present the frame
    bool (*resize)(uint32_t width, uint32_t height); // native px, cold path
    // State
    bool (*clear)(uint32_t color);                // 0xAARRGGBB, full drawable
    bool (*clip)(const Rectangle *rect);          // scissor; NULL = reset
    // Verbs — native pixels; each compiles to 1..N quads in a GPU backend.
    bool (*fillRect)(const Rectangle *rect, const Brush *brush);
    bool (*drawRect)(const Rectangle *rect, const Stroke *stroke);
    bool (*fillCircle)(float cx, float cy, float radius, const Brush *brush);
    bool (*drawCircle)(float cx, float cy, float radius, const Stroke *stroke);
    bool (*fillPath)(const Shape *shape, const Brush *brush);
    bool (*drawPath)(const Shape *shape, const Stroke *stroke);
    bool (*drawImage)(const Image *image, const Rectangle *dst); // dst comes last
} Graphics;

// Backend rows (one class pair per backend; VULKAN/METAL land later).
const Graphics *DirectGraphics_getRow(void);
const Graphics *VkGraphics_getRow(void);
const Graphics *MetalGraphics_getRow(void);

// Selection: stamps the row for backendId into the current context.
// Returns false on unknown id or an unbuilt backend row; on failure the
// previously selected row stays active (a null table never leaves the
// graphics context unusable).
bool Graphics_setGraphics(uint32_t backendId);

// Inspectors (null-safe): 0 while no backend is selected.
uint32_t Graphics_getGraphicsId(void);
const Graphics *Graphics_getCurrent(void);

// Forwarders — call through (*currentGraphics).<fn>; return false when
// no backend is selected or the row entry is null. Native pixel inputs.
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

#endif