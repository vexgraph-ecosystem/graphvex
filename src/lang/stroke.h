#ifndef LANG_STROKE_H
#define LANG_STROKE_H

#include <stdbool.h>
#include <stdint.h>

// lang/stroke.h — the outline stroke style (width + dash + cap + join + color).
//
// A Stroke is what an outline verb draws WITH: a width, a dash, a line cap and
// join, and a packed color. Plain data, embed-first, no GPU handles. Ported from
// the old reference (paint/stroke.h), reduced to the language contract.

#define STROKE_CAP_BUTT   0u
#define STROKE_CAP_ROUND  1u
#define STROKE_CAP_SQUARE 2u

#define STROKE_JOIN_MITER 0u
#define STROKE_JOIN_ROUND 1u
#define STROKE_JOIN_BEVEL 2u

typedef struct Stroke {
    float width;      // stroke width in pixels (>= 0; 0 = hairline)
    float dash;       // dash segment length in pixels (0 = solid)
    uint32_t cap;     // line cap (STROKE_CAP_*)
    uint32_t join;    // line join (STROKE_JOIN_*)
    uint32_t color;   // packed 0xRRGGBBAA (alpha low byte)
} Stroke;

// --- Constructors ---
//   Stroke()                -> width 1, solid, butt/miter, opaque black
//   Stroke(width, color)    -> solid, butt/miter
Stroke *Stroke_0(void);
Stroke *Stroke_2(float width, uint32_t color);

#define STROKE_CHOOSER(_0, _1, _2, NAME, ...) NAME
#define Stroke(...) STROKE_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Stroke_2, Stroke_1, Stroke_0 \
)(__VA_ARGS__)

// Release the stroke (null-safe no-op).
void Stroke_free(Stroke *stroke);

// --- Setters / Getters (the Symmetric Getter/Setter Completeness Law: null-safe) ---
void Stroke_setWidth(Stroke *stroke, float width);
void Stroke_setDash(Stroke *stroke, float dash);
void Stroke_setCap(Stroke *stroke, uint32_t cap);
void Stroke_setJoin(Stroke *stroke, uint32_t join);
void Stroke_setColor(Stroke *stroke, uint32_t color);
float Stroke_getWidth(const Stroke *stroke);
float Stroke_getDash(const Stroke *stroke);
uint32_t Stroke_getCap(const Stroke *stroke);
uint32_t Stroke_getJoin(const Stroke *stroke);
uint32_t Stroke_getColor(const Stroke *stroke);

#endif // LANG_STROKE_H
