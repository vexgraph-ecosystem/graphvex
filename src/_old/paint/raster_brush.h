#ifndef PAINT_RASTER_BRUSH_H
#define PAINT_RASTER_BRUSH_H

#include <stdint.h>
#include "c23/constructor.h"
#include "../graphics/type.h"
#include "paint/brush.h"

// paint/raster_brush.h — Stamp brush (embedded 32x32 R8 SDF tip + dynamics).
//
// A RasterBrush extends Brush by embedding it first (upcast with
// RasterBrush_asBrush to Brush). The tip is an embedded shape-only mask
// (255 = ink, 0 = clear); tint comes from the base color and the effective
// alpha is base opacity times stamp opacity. tipImage is an optional
// borrowed textured-tip override (never freed, null = procedural tip).

typedef struct Image Image;

#define RASTER_BRUSH_TIP_DIM 32u
#define RASTER_BRUSH_TIP_BYTES 1024u

#define RASTER_BRUSH_ROUND 0u
#define RASTER_BRUSH_SOFT 1u
#define RASTER_BRUSH_SQUARE 2u

typedef struct RasterBrush {
    // --- RasterBrush core (owner fields: base first for upcast) ---
    Brush base;              // embed-first base (color + opacity + typeId)
    // --- Tip part (embedded 32x32 R8 SDF stamp, shape-only) ---
    uint8_t tip[1024];       // 32x32 R8 mask bytes, row-major, 255 = ink
    // --- Dynamics part (per-dab stamp controls) ---
    float size;              // stamp diameter in pixels (> 0)
    float opacity;           // per-dab stamp alpha [0..1], mirrors base opacity
    float spacing;           // dab advance as fraction of size (0.25 = smooth)
    float jitter;            // positional jitter amount [0..1] (0 = off)
    float softness;          // stamping edge softness [0..1]
    // --- Tip image view (borrowed override, detach-only) ---
    Image *tipImage;         // borrowed textured tip (null = procedural); never freed
} RasterBrush;

// Default brush (round tip, size 16, opaque, spacing 0.25, no jitter, soft 0.5)
RasterBrush *RasterBrush_0(void);

// Preset brush (RASTER_BRUSH_ROUND / SOFT / SQUARE; bad preset yields round)
RasterBrush *RasterBrush_1(uint32_t preset);

// Release the brush (drops the borrowed tipImage, never frees it; null-safe)
void RasterBrush_free(RasterBrush *self);

// Core preset bakers (pure CPU distance math into the embedded tip, dest scribed)
void RasterBrush_makeRound(RasterBrush *dest);
void RasterBrush_makeSoft(RasterBrush *dest);
void RasterBrush_makeSquare(RasterBrush *dest);

// Upcast helpers (base is first member, borrowed view into self)
Brush *RasterBrush_asBrush(RasterBrush *self);
const Brush *RasterBrush_constBrush(const RasterBrush *self);

// Symmetric mutators (null-safe no-op on null self)
void RasterBrush_setColor(RasterBrush *self, uint32_t color);
void RasterBrush_setSize(RasterBrush *self, float size);
void RasterBrush_setOpacity(RasterBrush *self, float opacity);
void RasterBrush_setSpacing(RasterBrush *self, float spacing);
void RasterBrush_setJitter(RasterBrush *self, float jitter);
void RasterBrush_setSoftness(RasterBrush *self, float softness);
void RasterBrush_setTipPixel(RasterBrush *self, uint32_t x, uint32_t y, uint8_t v);
void RasterBrush_setTip(const uint8_t *src1024, RasterBrush *dest);
void RasterBrush_setTipImage(RasterBrush *self, Image *img);

// Null-safe inspectors (integers yield 0u/0, floats yield 0.0f, pointers null)
uint32_t RasterBrush_getColor(const RasterBrush *self);
uint64_t RasterBrush_getTypeId(const RasterBrush *self);
float RasterBrush_getSize(const RasterBrush *self);
float RasterBrush_getOpacity(const RasterBrush *self);
float RasterBrush_getSpacing(const RasterBrush *self);
float RasterBrush_getJitter(const RasterBrush *self);
float RasterBrush_getSoftness(const RasterBrush *self);
uint8_t RasterBrush_getTipPixel(const RasterBrush *self, uint32_t x, uint32_t y);
uint8_t *RasterBrush_tip(RasterBrush *self);
const uint8_t *RasterBrush_constTip(const RasterBrush *self);
Image *RasterBrush_getTipImage(const RasterBrush *self);

// Multi-value accessor (dest-last out, null-safe no-op on null out)
void RasterBrush_getTip(const RasterBrush *self, uint8_t *out1024);

#define RasterBrush(...) CONSTRUCTOR_DISPATCH(RasterBrush, __VA_ARGS__)
#endif
