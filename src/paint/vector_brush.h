#ifndef PAINT_VECTOR_BRUSH_H
#define PAINT_VECTOR_BRUSH_H

#include <stdint.h>
#include "c23/constructor.h"
#include "graphvex/type.h"
#include "paint/brush.h"

// paint/vector_brush.h — Gradient/pattern brush (grown stops + affine snap).
//
// A VectorBrush extends Brush by embedding it first (upcast with
// VectorBrush_asBrush to Brush). Stops live in vector space
// (resolution-free); pipelines resolve color from stops while tint and
// master alpha come from the embedded base. The stop table starts at
// VECTOR_BRUSH_STOPS_INIT and doubles on demand (the Dynamic Scalability &
// Anti-Hardcoding Law) — there is no stop ceiling.

#define VECTOR_BRUSH_STOPS_INIT 8u

typedef struct VectorBrush {
    // --- VectorBrush core (owner fields: base first for upcast) ---
    Brush base;            // embed-first base (color + opacity + typeId)
    // --- Stops part (heap-grown stop table) ---
    uint32_t *colors;      // packed stop colors (0xRRGGBBAA)
    float *offsets;        // stop positions [0..1] parallel to colors
    uint32_t stopCount;    // live stops in [0..stopCap]
    uint32_t stopCap;      // allocated stop slots (doubles on demand)
    // --- Transform part (affine snap for gradient space) ---
    float transform[6];    // 2D affine [a b c d tx ty], identity default
} VectorBrush;

// Default brush (black 0x000000FF to white 0xFFFFFFFF stops, identity transform, opaque base)
VectorBrush *VectorBrush_0(void);

// Two-stop gradient (offsets 0 and 1, identity transform, opaque base)
VectorBrush *VectorBrush_2(uint32_t c0, uint32_t c1);

// Release the brush (null-safe no-op)
void VectorBrush_free(VectorBrush *self);

// Upcast helpers (base is first member, borrowed view into self)
Brush *VectorBrush_asBrush(VectorBrush *self);
const Brush *VectorBrush_constBrush(const VectorBrush *self);

// Symmetric mutators for the embedded base (null-safe no-op on null self)
void VectorBrush_setColor(VectorBrush *self, uint32_t color);
void VectorBrush_setOpacity(VectorBrush *self, float opacity);

// Symmetric mutators for stops + transform (null-safe no-op on null self).
// Growable stop verbs (setStopCount/setStop/setColors/setOffsets) reserve
// capacity first and are a null-safe no-op if that reservation fails (OOM),
// keeping the current stops (the Cold-Strict, Hot-Minimal Validation Law).
void VectorBrush_setStopCount(VectorBrush *self, uint32_t n);
void VectorBrush_setStop(VectorBrush *self, uint32_t index, uint32_t color, float offset);
void VectorBrush_setColors(const uint32_t *colors, uint32_t count, VectorBrush *dest);
void VectorBrush_setOffsets(const float *offsets, uint32_t count, VectorBrush *dest);
void VectorBrush_setTransform(VectorBrush *self, const float *m6);
void VectorBrush_setTransformAt(VectorBrush *self, uint32_t i, float v);

// Null-safe inspectors (integers yield 0u/0, floats yield 0.0f)
uint32_t VectorBrush_getColor(const VectorBrush *self);
float VectorBrush_getOpacity(const VectorBrush *self);
uint64_t VectorBrush_getTypeId(const VectorBrush *self);
uint32_t VectorBrush_getStopCount(const VectorBrush *self);
uint32_t VectorBrush_getStopColor(const VectorBrush *self, uint32_t index);
float VectorBrush_getStopOffset(const VectorBrush *self, uint32_t index);
float VectorBrush_getTransformAt(const VectorBrush *self, uint32_t i);

// Multi-value accessors (dest-last outs, null-safe no-op on null out)
void VectorBrush_getStop(const VectorBrush *self, uint32_t index, uint32_t *outColor, float *outOffset);
void VectorBrush_getColors(const VectorBrush *self, uint32_t *outColors, uint32_t *outCount);
void VectorBrush_getOffsets(const VectorBrush *self, float *outOffsets, uint32_t *outCount);
void VectorBrush_getTransform(const VectorBrush *self, float *outM6);

#define VectorBrush(...) CONSTRUCTOR_DISPATCH(VectorBrush, __VA_ARGS__)
#endif
