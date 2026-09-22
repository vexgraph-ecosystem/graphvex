#ifndef LANG_BRUSH_H
#define LANG_BRUSH_H

#include <stdbool.h>
#include <stdint.h>

// lang/brush.h — the fill brush (color + opacity, plain data).
//
// A Brush is what a fill verb paints WITH: a packed color and a master alpha
// multiplier. It is embed-first plain data — no GPU handles, no behavior beyond
// symmetric accessors. Ported from the old reference (paint/brush.h), reduced to
// the language contract (no block-header type id).

typedef struct Brush {
    uint32_t color;    // packed 0xRRGGBBAA (alpha low byte — the Strict 0xRRGGBBAA Color Law)
    float opacity;     // master alpha multiplier [0..1]
} Brush;

// --- Constructors ---
//   Brush()                     -> opaque black, opacity 1
//   Brush(color, opacity)       -> packed 0xRRGGBBAA + alpha
Brush *Brush_0(void);
Brush *Brush_2(uint32_t color, float opacity);

#define BRUSH_CHOOSER(_0, _1, _2, NAME, ...) NAME
#define Brush(...) BRUSH_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Brush_2, Brush_1, Brush_0 \
)(__VA_ARGS__)

// Release the brush (null-safe no-op).
void Brush_free(Brush *brush);

// --- Setters / Getters (the Symmetric Getter/Setter Completeness Law: null-safe) ---
void Brush_setColor(Brush *brush, uint32_t color);
void Brush_setOpacity(Brush *brush, float opacity);
uint32_t Brush_getColor(const Brush *brush);
float Brush_getOpacity(const Brush *brush);

#endif // LANG_BRUSH_H
