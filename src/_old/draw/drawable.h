#ifndef DRAW_DRAWABLE_H
#define DRAW_DRAWABLE_H

#include <stdbool.h>
#include <stdint.h>
#include "c23/constructor.h"
#include "../graphics/type.h"
#include "image/image.h"
#include "paint/brush.h"
#include "paint/stroke.h"
#include "vector/shape.h"

// draw/drawable.h — Single-layer raster board owning one Image; CPU stub.
//
// A Drawable is the one-layer raster board: every fill*/draw* verb hangs
// here. It owns exactly one Image backing (Image_2 allocated, freed by
// Drawable_free, never borrowed). This scaffold records verbs on CPU only:
// each verb null-guards and marks the dirty flag without painting pixels.
// Actual GPU recording comes later; no Vulkan includes ever live here.

typedef struct Drawable {
    Image *owned;    // OWNED raster backing (never borrowed); freed by Drawable_free
    uint64_t typeId; // block-header type id (TYPE_DRAWABLE_SINGLETON)
    bool dirty;      // true once any fill/draw/clear verb recorded or backing replaced
} Drawable;

// Empty 1x1 board; null on OOM
Drawable *Drawable_0(void);

// Sized board (w/h in pixels); null on zero dims or OOM
Drawable *Drawable_2(uint32_t w, uint32_t h);

// CPU-stub verbs: null-guard + mark dirty, no pixels painted yet
void Drawable_fillRect(Drawable *self, float x, float y, float w, float h, const Brush *brush);
void Drawable_drawRect(Drawable *self, float x, float y, float w, float h, const Stroke *stroke);
void Drawable_fillCircle(Drawable *self, float cx, float cy, float r, const Brush *brush);
void Drawable_drawCircle(Drawable *self, float cx, float cy, float r, const Stroke *stroke);
void Drawable_fillPath(Drawable *self, const Shape *shape, const Brush *brush);
void Drawable_drawPath(Drawable *self, const Shape *shape, const Stroke *stroke);

// Clear stub (color is packed 0xRRGGBBAA like Brush/Stroke): null-guard + mark dirty
void Drawable_clear(Drawable *self, uint32_t color);

// Release the owned Image then the struct (null-safe no-op)
void Drawable_free(Drawable *self);

// Symmetric mutators (null-safe no-op on null self)
void Drawable_setImage(Drawable *self, Image *img);
void Drawable_setDirty(Drawable *self, bool dirty);

// Null-safe inspectors (image yields nullptr, dirty yields false, sizes yield 0)
Image *Drawable_getImage(const Drawable *self);
bool Drawable_isDirty(const Drawable *self);
uint32_t Drawable_getWidth(const Drawable *self);
uint32_t Drawable_getHeight(const Drawable *self);

#define Drawable(...) CONSTRUCTOR_DISPATCH(Drawable, __VA_ARGS__)
#endif
