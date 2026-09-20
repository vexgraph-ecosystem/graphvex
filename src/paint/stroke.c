#include "paint/stroke.h"

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
 * DEFINITION: Stroke
 * ============================================================================
 * Plain-data geometric outline style defining line width, dash metrics, caps,
 * joins, and packed color for vector path stroke rasterization. Consumed directly
 * by vector path drawing pipelines without carrying GPU driver handles.
 *
 * Enforces packed 32-bit color formatted as 0xRRGGBBAA under the Strict 0xRRGGBBAA
 * Color Law. Struct instances ride the vexspoke memory arena (TYPE_STROKE_SINGLETON)
 * with a calloc fallback for standalone builds. Vector pipelines copy the plain-data
 * struct by value without manual retention or aliasing.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Stroke (paint/stroke.c)
 * LEVEL: L2 — Behavior (plain-data stroke style API)
 * ============================================================================
 * SUMMARY:
 *   Plain-data stroke style encapsulating width, dash length, cap style,
 *   join style, and 0xRRGGBBAA packed outline color.
 *
 * STRUCT FIELDS (Mirroring paint/stroke.h):
 * ----------------------------------------------------------------------------
 *   float width;      // stroke width in pixels (>= 0; 0 = hairline)
 *   float dash;       // dash segment length in pixels (0 = solid)
 *   uint32_t cap;     // line cap (STROKE_CAP_*)
 *   uint32_t join;    // line join (STROKE_JOIN_*)
 *   uint32_t color;   // packed color (0xRRGGBBAA)
 *   uint64_t typeId;  // block-header type id (TYPE_STROKE_SINGLETON)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Stroke_0(void)                                     : Default stroke (width 1, butt cap, miter join, 0x000000FF)
 *   - Stroke_2(width, color)                             : Width and color stroke
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Stroke_free(self)                                  : Release stroke struct memory
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - Stroke_setWidth(self, width)                       : Mutate stroke width
 *   - Stroke_setDash(self, dash)                         : Mutate dash segment length
 *   - Stroke_setCap(self, cap)                           : Mutate line cap style
 *   - Stroke_setJoin(self, join)                         : Mutate line join style
 *   - Stroke_setColor(self, color)                       : Mutate 0xRRGGBBAA outline color
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Stroke_getWidth(self)                              : Query stroke width
 *   - Stroke_getDash(self)                               : Query dash segment length
 *   - Stroke_getCap(self)                                : Query line cap style
 *   - Stroke_getJoin(self)                               : Query line join style
 *   - Stroke_getColor(self)                              : Query 0xRRGGBBAA outline color
 *   - Stroke_getTypeId(self)                             : Query type identity stamp
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

Stroke *Stroke_0(void) {
    return Stroke_2(1.0f, 0x000000FFu);
}

Stroke *Stroke_2(float width, uint32_t color) {
    Stroke *self = (Stroke*) Memory_alloc(TYPE_STROKE_SINGLETON, sizeof(Stroke));
    if (!self)
        self = (Stroke*) calloc(1, sizeof(Stroke));
    if (!self)
        return nullptr;
    (*self).width = width;
    (*self).dash = 0.0f;
    (*self).cap = STROKE_CAP_BUTT;
    (*self).join = STROKE_JOIN_MITER;
    (*self).color = color;
    (*self).typeId = TYPE_STROKE_SINGLETON;
    return self;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

void Stroke_free(Stroke *self) {
    if (!self)
        return;
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void Stroke_setWidth(Stroke *self, float width) {
    if (!self)
        return;
    (*self).width = width;
}

;;SETTER
void Stroke_setDash(Stroke *self, float dash) {
    if (!self)
        return;
    (*self).dash = dash;
}

;;SETTER
void Stroke_setCap(Stroke *self, uint32_t cap) {
    if (!self)
        return;
    (*self).cap = cap;
}

;;SETTER
void Stroke_setJoin(Stroke *self, uint32_t join) {
    if (!self)
        return;
    (*self).join = join;
}

;;SETTER
void Stroke_setColor(Stroke *self, uint32_t color) {
    if (!self)
        return;
    (*self).color = color;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
float Stroke_getWidth(const Stroke *self) {
    return self ? (*self).width : 0.0f;
}

;;GETTER
float Stroke_getDash(const Stroke *self) {
    return self ? (*self).dash : 0.0f;
}

;;GETTER
uint32_t Stroke_getCap(const Stroke *self) {
    return self ? (*self).cap : 0u;
}

;;GETTER
uint32_t Stroke_getJoin(const Stroke *self) {
    return self ? (*self).join : 0u;
}

;;GETTER
uint32_t Stroke_getColor(const Stroke *self) {
    return self ? (*self).color : 0u;
}

;;GETTER
uint64_t Stroke_getTypeId(const Stroke *self) {
    return self ? (*self).typeId : 0;
}
