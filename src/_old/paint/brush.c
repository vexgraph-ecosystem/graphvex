#include "paint/brush.h"

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
 * DEFINITION: Brush
 * ============================================================================
 * Fundamental solid paint primitive defining surface pigmentation and opacity
 * across vector and raster rendering operations. Serves as the embed-first base
 * record inherited directly by higher-level brushes (VectorBrush, RasterBrush).
 *
 * Stores packed 32-bit color strictly formatted as 0xRRGGBBAA under the Strict
 * 0xRRGGBBAA Color Law, alongside a normalized master opacity scalar clamped
 * within [0.0..1.0]. Owns no device handles or driver allocations; pipelines
 * evaluate effective fragment color by multiplying RGB by opacity. Struct
 * allocation rides the vexspoke memory arena (TYPE_BRUSH_SINGLETON) with
 * standalone heap fallback.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Brush (paint/brush.c)
 * LEVEL: L2 — Behavior (base solid brush API)
 * ============================================================================
 * SUMMARY:
 *   Base solid brush encapsulation combining packed 0xRRGGBBAA color with
 *   master opacity. Embed-first base record inherited by composite brushes.
 *
 * STRUCT FIELDS (Mirroring paint/brush.h):
 * ----------------------------------------------------------------------------
 *   uint32_t color;   // packed base color (0xRRGGBBAA)
 *   float opacity;    // master alpha multiplier [0..1]
 *   uint64_t typeId;  // block-header type id (TYPE_BRUSH_SINGLETON)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Brush_0(void)                                      : Default brush (opaque black 0x000000FF, opacity 1.0)
 *   - Brush_2(color, opacity)                            : Specified color and opacity brush
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Brush_free(self)                                   : Release brush struct memory
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - Brush_setColor(self, color)                        : Mutate packed 0xRRGGBBAA base color
 *   - Brush_setOpacity(self, opacity)                    : Mutate master opacity scalar
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Brush_getColor(self)                               : Query packed 0xRRGGBBAA color
 *   - Brush_getOpacity(self)                             : Query master opacity scalar
 *   - Brush_getTypeId(self)                              : Query type identity stamp
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

Brush *Brush_0(void) {
    return Brush_2(0x000000FFu, 1.0f);
}

Brush *Brush_2(uint32_t color, float opacity) {
    Brush *self = (Brush*) Memory_alloc(TYPE_BRUSH_SINGLETON, sizeof(Brush));
    if (!self)
        self = (Brush*) calloc(1, sizeof(Brush));
    if (!self)
        return nullptr;
    (*self).color = color;
    (*self).opacity = opacity;
    (*self).typeId = TYPE_BRUSH_SINGLETON;
    return self;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

void Brush_free(Brush *self) {
    if (!self)
        return;
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void Brush_setColor(Brush *self, uint32_t color) {
    if (!self)
        return;
    (*self).color = color;
}

;;SETTER
void Brush_setOpacity(Brush *self, float opacity) {
    if (!self)
        return;
    (*self).opacity = opacity;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t Brush_getColor(const Brush *self) {
    return self ? (*self).color : 0u;
}

;;GETTER
float Brush_getOpacity(const Brush *self) {
    return self ? (*self).opacity : 0.0f;
}

;;GETTER
uint64_t Brush_getTypeId(const Brush *self) {
    return self ? (*self).typeId : 0;
}
