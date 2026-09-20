#ifndef PAINT_STROKE_H
#define PAINT_STROKE_H

#include <stdint.h>
#include "c23/constructor.h"
#include "graphvex/type.h"

// paint/stroke.h — Plain-data stroke style (width + dash + cap + join + color).
//
// A Stroke carries no behavior and no GPU handles: it is the embeddable
// value record consumed by drawable/vector pipelines. VectorBrush-style
// owners keep their own copies; nothing here is borrowed or freed by hand.

#define STROKE_CAP_BUTT 0u
#define STROKE_CAP_ROUND 1u
#define STROKE_CAP_SQUARE 2u

#define STROKE_JOIN_MITER 0u
#define STROKE_JOIN_ROUND 1u
#define STROKE_JOIN_BEVEL 2u

typedef struct Stroke {
    float width;      // stroke width in pixels (>= 0; 0 = hairline)
    float dash;       // dash segment length in pixels (0 = solid)
    uint32_t cap;     // line cap (STROKE_CAP_* )
    uint32_t join;    // line join (STROKE_JOIN_* )
    uint32_t color;   // packed color (0xAARRGGBB: alpha high byte)
    uint64_t typeId;  // block-header type id (TYPE_STROKE_SINGLETON)
} Stroke;

// Default stroke (width 1, solid, butt cap, miter join, opaque black 0xFF000000)
Stroke *Stroke_0(void);

// Width + color stroke (solid, butt cap, miter join)
Stroke *Stroke_2(float width, uint32_t color);

// Release the stroke (null-safe no-op)
void Stroke_free(Stroke *self);

// Symmetric mutators (null-safe no-op on null self)
void Stroke_setWidth(Stroke *self, float width);
void Stroke_setDash(Stroke *self, float dash);
void Stroke_setCap(Stroke *self, uint32_t cap);
void Stroke_setJoin(Stroke *self, uint32_t join);
void Stroke_setColor(Stroke *self, uint32_t color);

// Null-safe inspectors (floats yield 0.0f, integers yield 0u/0)
float Stroke_getWidth(const Stroke *self);
float Stroke_getDash(const Stroke *self);
uint32_t Stroke_getCap(const Stroke *self);
uint32_t Stroke_getJoin(const Stroke *self);
uint32_t Stroke_getColor(const Stroke *self);
uint64_t Stroke_getTypeId(const Stroke *self);

#define Stroke(...) CONSTRUCTOR_DISPATCH(Stroke, __VA_ARGS__)
#endif
