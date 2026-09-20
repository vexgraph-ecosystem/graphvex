#include "draw/drawable.h"

#include <stdlib.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Drawable
 * ============================================================================
 * Single-layer raster drawing board abstraction owning a backing Image.
 * Acts as the immediate 2D primitive rendering target for brush fills, stroke outlines,
 * path sweeps, and surface color clears.
 *
 * Implements fine-grained dirty state tracking and automatic backing image lifecycle
 * management in compliance with the Unified Graphics Abstraction Law and the
 * Strict 0xRRGGBBAA Color Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Drawable (draw/drawable.c)
 * LEVEL: L3 — Module Code (single-layer raster board behavior)
 * ============================================================================
 * Single-layer raster board owning one Image. CPU stub: every fill/draw
 * verb null-guards and marks the dirty flag without painting pixels yet;
 * actual GPU recording comes later. No Vulkan includes here.
 *
 * STRUCT FIELDS (Mirroring draw/drawable.h):
 * ----------------------------------------------------------------------------
 *   Drawable {
 *     Image *owned;    // OWNED raster backing (never borrowed); freed by Drawable_free
 *     uint64_t typeId; // block-header type id (TYPE_DRAWABLE_SINGLETON)
 *     bool dirty;      // true once any fill/draw/clear verb recorded or backing replaced
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Drawable_0(void)                                      : Allocate default 1x1 drawable
 *   - Drawable_2(w, h)                                      : Allocate sized drawable
 *
 * Private Constructors: (.c static)
 *   - drawableCreate(w, h)                                  : Allocate and initialize drawable
 *
 * Public Core Functions: (.h)
 *   - Drawable_fillRect(self, x, y, w, h, brush)           : Draw filled rectangle
 *   - Drawable_drawRect(self, x, y, w, h, stroke)          : Draw stroked rectangle
 *   - Drawable_fillCircle(self, cx, cy, r, brush)          : Draw filled circle
 *   - Drawable_drawCircle(self, cx, cy, r, stroke)         : Draw stroked circle
 *   - Drawable_fillPath(self, shape, brush)                : Draw filled vector path
 *   - Drawable_drawPath(self, shape, stroke)               : Draw stroked vector path
 *   - Drawable_clear(self, color)                          : Clear raster to 0xRRGGBBAA color
 *   - Drawable_free(self)                                  : Release board and owned image
 *
 * Private Core Functions: (.c static)
 *   - drawableFreeStorage(self)                             : Deallocate heap memory
 *
 * Public Setters: (.h)
 *   - Drawable_setImage(self, img)                         : Replace owned backing image
 *   - Drawable_setDirty(self, dirty)                       : Set board dirty flag
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Drawable_getImage(self)                              : Query owned backing image
 *   - Drawable_isDirty(self)                               : Query dirty status flag
 *   - Drawable_getWidth(self)                              : Query raster pixel width
 *   - Drawable_getHeight(self)                             : Query raster pixel height
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

static void drawableFreeStorage(Drawable *self) {
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

static Drawable *drawableCreate(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0)
        return nullptr;
    Drawable *self = (Drawable*) Memory_alloc(TYPE_DRAWABLE_SINGLETON, sizeof(Drawable));
    if (!self)
        self = (Drawable*) calloc(1, sizeof(Drawable));
    if (!self)
        return nullptr;
    Image *backing = Image_2(w, h);
    if (!backing) {
        drawableFreeStorage(self);
        return nullptr;
    }
    (*self).owned = backing;
    (*self).typeId = TYPE_DRAWABLE_SINGLETON;
    (*self).dirty = false;
    return self;
}

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Drawable *Drawable_0(void) {
    return drawableCreate(1, 1);
}

Drawable *Drawable_2(uint32_t w, uint32_t h) {
    return drawableCreate(w, h);
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void Drawable_fillRect(Drawable *self, float x, float y, float w, float h, const Brush *brush) {
    if (!self)
        return;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)brush;
    (*self).dirty = true;
}

void Drawable_drawRect(Drawable *self, float x, float y, float w, float h, const Stroke *stroke) {
    if (!self)
        return;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)stroke;
    (*self).dirty = true;
}

void Drawable_fillCircle(Drawable *self, float cx, float cy, float r, const Brush *brush) {
    if (!self)
        return;
    (void)cx;
    (void)cy;
    (void)r;
    (void)brush;
    (*self).dirty = true;
}

void Drawable_drawCircle(Drawable *self, float cx, float cy, float r, const Stroke *stroke) {
    if (!self)
        return;
    (void)cx;
    (void)cy;
    (void)r;
    (void)stroke;
    (*self).dirty = true;
}

void Drawable_fillPath(Drawable *self, const Shape *shape, const Brush *brush) {
    if (!self)
        return;
    (void)shape;
    (void)brush;
    (*self).dirty = true;
}

void Drawable_drawPath(Drawable *self, const Shape *shape, const Stroke *stroke) {
    if (!self)
        return;
    (void)shape;
    (void)stroke;
    (*self).dirty = true;
}

void Drawable_clear(Drawable *self, uint32_t color) {
    if (!self)
        return;
    (void)color;
    (*self).dirty = true;
}

void Drawable_free(Drawable *self) {
    if (!self)
        return;
    Image_free((*self).owned);
    (*self).owned = nullptr;
    drawableFreeStorage(self);
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void Drawable_setImage(Drawable *self, Image *img) {
    if (!self)
        return;
    if (img == (*self).owned)
        return;
    Image_free((*self).owned);
    (*self).owned = img;
    (*self).dirty = true;
}

;;SETTER
void Drawable_setDirty(Drawable *self, bool dirty) {
    if (!self)
        return;
    (*self).dirty = dirty;
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
Image *Drawable_getImage(const Drawable *self) {
    return self ? (*self).owned : nullptr;
}

;;GETTER
bool Drawable_isDirty(const Drawable *self) {
    return self ? (*self).dirty : false;
}

;;GETTER
uint32_t Drawable_getWidth(const Drawable *self) {
    if (!self)
        return 0;
    return Image_getWidth((*self).owned);
}

;;GETTER
uint32_t Drawable_getHeight(const Drawable *self) {
    if (!self)
        return 0;
    return Image_getHeight((*self).owned);
}
