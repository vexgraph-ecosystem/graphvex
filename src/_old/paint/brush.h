#ifndef PAINT_BRUSH_H
#define PAINT_BRUSH_H

#include <stdint.h>
#include "c23/constructor.h"
#include "../graphics/type.h"

// paint/brush.h — Base solid brush (color + opacity, plain data).
//
// A Brush is the embed-first base record: VectorBrush/RasterBrush declare a
// Brush as their first member (never created here). No GPU handles live in
// this struct; pipelines resolve the solid fill from color * opacity.

typedef struct Brush {
    uint32_t color;   // packed base color (0xRRGGBBAA: alpha low byte)
    float opacity;    // master alpha multiplier [0..1]
    uint64_t typeId;  // block-header type id (TYPE_BRUSH_SINGLETON)
} Brush;

// Default brush (opaque black 0x000000FF, opacity 1)
Brush *Brush_0(void);

// Color + opacity brush
Brush *Brush_2(uint32_t color, float opacity);

// Release the brush (null-safe no-op)
void Brush_free(Brush *self);

// Symmetric mutators (null-safe no-op on null self)
void Brush_setColor(Brush *self, uint32_t color);
void Brush_setOpacity(Brush *self, float opacity);

// Null-safe inspectors (integers yield 0u/0, opacity yields 0.0f)
uint32_t Brush_getColor(const Brush *self);
float Brush_getOpacity(const Brush *self);
uint64_t Brush_getTypeId(const Brush *self);

#define Brush(...) CONSTRUCTOR_DISPATCH(Brush, __VA_ARGS__)
#endif
