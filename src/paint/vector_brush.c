#include "paint/vector_brush.h"

#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VectorBrush
 * ============================================================================
 * Resolution-independent gradient and procedural pattern brush extending the base
 * Brush primitive with an arbitrarily sized array of color stops and an affine
 * transformation matrix. Evaluates smooth color interpolations across vector shapes
 * and canvas fills.
 *
 * Encapsulates color stops formatted strictly as 0xRRGGBBAA under the Strict
 * 0xRRGGBBAA Color Law, pairing each stop with a normalized scalar offset [0.0..1.0].
 * Stop tables dynamically expand from an initial capacity of 8 slots, doubling on
 * demand without arbitrary ceilings. The base Brush sub-object resides at offset 0
 * for direct upcasting. Struct memory rides the vexspoke typed memory arena
 * (TYPE_VECTOR_BRUSH_SINGLETON) with standard heap fallback.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VectorBrush (paint/vector_brush.c)
 * LEVEL: L2 — Behavior (gradient brush behavior API)
 * ============================================================================
 * SUMMARY:
 *   Gradient and pattern brush in vector space with heap-grown 0xRRGGBBAA color
 *   stops and a 6-float affine transformation. Embeds Brush at offset 0.
 *
 * STRUCT FIELDS (Mirroring paint/vector_brush.h):
 * ----------------------------------------------------------------------------
 *   Brush base;            // embed-first base (color + opacity + typeId)
 *   uint32_t *colors;      // packed stop colors (0xRRGGBBAA)
 *   float *offsets;        // stop positions [0..1] parallel to colors
 *   uint32_t stopCount;    // live stops in [0..stopCap]
 *   uint32_t stopCap;      // allocated stop slots (INIT 8, doubles on demand)
 *   float transform[6];    // 2D affine [a b c d tx ty], identity default
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - VectorBrush_0(void)                                : Default brush (black 0x000000FF to white 0xFFFFFFFF)
 *   - VectorBrush_2(c0, c1)                              : Two-stop gradient brush
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - VectorBrush_free(self)                             : Release brush and owned stop arrays
 *   - VectorBrush_asBrush(self)                          : Borrow base Brush pointer
 *   - VectorBrush_constBrush(self)                       : Borrow const base Brush pointer
 *
 * Private Core Functions: (.c static)
 *   - vectorBrushFreeStorage(self)                       : Deallocate arena or heap storage
 *   - vectorBrushReserveStops(self, need)                : Grow stop table to accommodate capacity
 *   - vectorBrushInitStops(self, c0, c1)                 : Seed initial two-stop gradient
 *   - vectorBrushInitTransform(self)                     : Initialize identity affine matrix
 *
 * Public Setters: (.h)
 *   - VectorBrush_setColor(self, color)                  : Mutate embedded base 0xRRGGBBAA color
 *   - VectorBrush_setOpacity(self, opacity)              : Mutate embedded base opacity
 *   - VectorBrush_setStopCount(self, n)                  : Resize live stop count
 *   - VectorBrush_setStop(self, index, color, offset)    : Mutate single stop color and offset
 *   - VectorBrush_setColors(colors, count, dest)         : Bulk replace stop colors
 *   - VectorBrush_setOffsets(offsets, count, dest)       : Bulk replace stop offsets
 *   - VectorBrush_setTransform(self, m6)                 : Set 6-float affine transform
 *   - VectorBrush_setTransformAt(self, i, v)             : Set individual transform element
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - VectorBrush_getColor(self)                         : Query embedded base color
 *   - VectorBrush_getOpacity(self)                       : Query embedded base opacity
 *   - VectorBrush_getTypeId(self)                        : Query type identity stamp
 *   - VectorBrush_getStopCount(self)                     : Query live stop count
 *   - VectorBrush_getStopColor(self, index)              : Query individual stop color
 *   - VectorBrush_getStopOffset(self, index)             : Query individual stop offset
 *   - VectorBrush_getTransformAt(self, i)                : Query individual transform element
 *   - VectorBrush_getStop(self, index, outColor, outOffset) : Query single stop tuple
 *   - VectorBrush_getColors(self, outColors, outCount)   : Bulk read stop colors
 *   - VectorBrush_getOffsets(self, outOffsets, outCount) : Bulk read stop offsets
 *   - VectorBrush_getTransform(self, outM6)              : Read 6-float affine transform
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

static void vectorBrushFreeStorage(VectorBrush *self);
static bool vectorBrushReserveStops(VectorBrush *self, uint32_t need);
static void vectorBrushInitStops(VectorBrush *self, uint32_t c0, uint32_t c1);
static void vectorBrushInitTransform(VectorBrush *self);

VectorBrush *VectorBrush_0(void) {
    return VectorBrush_2(0x000000FFu, 0xFFFFFFFFu);
}

VectorBrush *VectorBrush_2(uint32_t c0, uint32_t c1) {
    VectorBrush *self = (VectorBrush*) Memory_alloc(TYPE_VECTOR_BRUSH_SINGLETON, sizeof(VectorBrush));
    if (!self)
        self = (VectorBrush*) calloc(1, sizeof(VectorBrush));
    if (!self)
        return nullptr;
    Brush *b = &(*self).base;
    (*b).color = 0x000000FFu;
    (*b).opacity = 1.0f;
    (*b).typeId = TYPE_VECTOR_BRUSH_SINGLETON;
    vectorBrushInitStops(self, c0, c1);
    vectorBrushInitTransform(self);
    return self;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

static void vectorBrushFreeStorage(VectorBrush *self) {
    Memory_free((*self).colors);
    Memory_free((*self).offsets);
    (*self).colors = NULL;
    (*self).offsets = NULL;
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

static bool vectorBrushReserveStops(VectorBrush *self, uint32_t need) {
    if ((*self).stopCap >= need)
        return true;
    uint32_t oldCap = (*self).stopCap;
    uint32_t newCap = oldCap == 0u ? VECTOR_BRUSH_STOPS_INIT : oldCap;
    while (newCap < need)
        newCap *= 2u;
    uint32_t *colors = (*self).colors
        ? (uint32_t*) Memory_realloc((*self).colors, (size_t) newCap * sizeof(uint32_t))
        : (uint32_t*) Memory_alloc(TYPE_ARRAY, (size_t) newCap * sizeof(uint32_t));
    if (!colors)
        return false;
    float *offsets = (*self).offsets
        ? (float*) Memory_realloc((*self).offsets, (size_t) newCap * sizeof(float))
        : (float*) Memory_alloc(TYPE_ARRAY, (size_t) newCap * sizeof(float));
    if (!offsets) {
        (*self).colors = colors;
        return false;
    }
    for (uint32_t i = oldCap; i < newCap; i++) {
        colors[i] = 0x000000FFu;
        offsets[i] = 1.0f;
    }
    (*self).colors = colors;
    (*self).offsets = offsets;
    (*self).stopCap = newCap;
    return true;
}

static void vectorBrushInitStops(VectorBrush *self, uint32_t c0, uint32_t c1) {
    if (!vectorBrushReserveStops(self, 2u))
        return;
    (*self).colors[0] = c0;
    (*self).colors[1] = c1;
    (*self).offsets[0] = 0.0f;
    (*self).offsets[1] = 1.0f;
    (*self).stopCount = 2;
}

static void vectorBrushInitTransform(VectorBrush *self) {
    (*self).transform[0] = 1.0f;
    (*self).transform[1] = 0.0f;
    (*self).transform[2] = 0.0f;
    (*self).transform[3] = 1.0f;
    (*self).transform[4] = 0.0f;
    (*self).transform[5] = 0.0f;
}

void VectorBrush_free(VectorBrush *self) {
    if (!self)
        return;
    vectorBrushFreeStorage(self);
}

Brush *VectorBrush_asBrush(VectorBrush *self) {
    return (Brush*) self;
}

const Brush *VectorBrush_constBrush(const VectorBrush *self) {
    return (const Brush*) self;
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void VectorBrush_setColor(VectorBrush *self, uint32_t color) {
    if (!self)
        return;
    Brush *b = &(*self).base;
    (*b).color = color;
}

;;SETTER
void VectorBrush_setOpacity(VectorBrush *self, float opacity) {
    if (!self)
        return;
    Brush *b = &(*self).base;
    (*b).opacity = opacity;
}

;;SETTER
void VectorBrush_setStopCount(VectorBrush *self, uint32_t n) {
    if (!self)
        return;
    if (n == 0u) {
        (*self).stopCount = 0u;
        return;
    }
    if (!vectorBrushReserveStops(self, n))
        return;
    (*self).stopCount = n;
}

;;SETTER
void VectorBrush_setStop(VectorBrush *self, uint32_t index, uint32_t color, float offset) {
    if (!self)
        return;
    if (index == UINT32_MAX || !vectorBrushReserveStops(self, index + 1u))
        return;
    (*self).colors[index] = color;
    (*self).offsets[index] = offset;
    if ((*self).stopCount < index + 1u)
        (*self).stopCount = index + 1u;
}

;;SETTER
void VectorBrush_setColors(const uint32_t *colors, uint32_t count, VectorBrush *dest) {
    if (!colors || !dest)
        return;
    if (count == 0u || !vectorBrushReserveStops(dest, count))
        return;
    memcpy((*dest).colors, colors, (size_t)count * sizeof(uint32_t));
    (*dest).stopCount = count;
}

;;SETTER
void VectorBrush_setOffsets(const float *offsets, uint32_t count, VectorBrush *dest) {
    if (!offsets || !dest)
        return;
    if (count == 0u || !vectorBrushReserveStops(dest, count))
        return;
    memcpy((*dest).offsets, offsets, (size_t)count * sizeof(float));
    if ((*dest).stopCount < count)
        (*dest).stopCount = count;
}

;;SETTER
void VectorBrush_setTransform(VectorBrush *self, const float *m6) {
    if (!self || !m6)
        return;
    memcpy((*self).transform, m6, 6 * sizeof(float));
}

;;SETTER
void VectorBrush_setTransformAt(VectorBrush *self, uint32_t i, float v) {
    if (!self)
        return;
    if (i >= 6)
        return;
    (*self).transform[i] = v;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t VectorBrush_getColor(const VectorBrush *self) {
    if (!self)
        return 0u;
    const Brush *b = &(*self).base;
    return (*b).color;
}

;;GETTER
float VectorBrush_getOpacity(const VectorBrush *self) {
    if (!self)
        return 0.0f;
    const Brush *b = &(*self).base;
    return (*b).opacity;
}

;;GETTER
uint64_t VectorBrush_getTypeId(const VectorBrush *self) {
    if (!self)
        return 0;
    const Brush *b = &(*self).base;
    return (*b).typeId;
}

;;GETTER
uint32_t VectorBrush_getStopCount(const VectorBrush *self) {
    return self ? (*self).stopCount : 0u;
}

;;GETTER
uint32_t VectorBrush_getStopColor(const VectorBrush *self, uint32_t index) {
    if (!self || index >= (*self).stopCap)
        return 0u;
    return (*self).colors[index];
}

;;GETTER
float VectorBrush_getStopOffset(const VectorBrush *self, uint32_t index) {
    if (!self || index >= (*self).stopCap)
        return 0.0f;
    return (*self).offsets[index];
}

;;GETTER
float VectorBrush_getTransformAt(const VectorBrush *self, uint32_t i) {
    if (!self || i >= 6)
        return 0.0f;
    return (*self).transform[i];
}

;;GETTER
void VectorBrush_getStop(const VectorBrush *self, uint32_t index, uint32_t *outColor, float *outOffset) {
    if (!self || index >= (*self).stopCap) {
        if (outColor)
            *outColor = 0u;
        if (outOffset)
            *outOffset = 0.0f;
        return;
    }
    if (outColor)
        *outColor = (*self).colors[index];
    if (outOffset)
        *outOffset = (*self).offsets[index];
}

;;GETTER
void VectorBrush_getColors(const VectorBrush *self, uint32_t *outColors, uint32_t *outCount) {
    if (!self) {
        if (outCount)
            *outCount = 0u;
        return;
    }
    if (outColors)
        memcpy(outColors, (*self).colors, (size_t)(*self).stopCount * sizeof(uint32_t));
    if (outCount)
        *outCount = (*self).stopCount;
}

;;GETTER
void VectorBrush_getOffsets(const VectorBrush *self, float *outOffsets, uint32_t *outCount) {
    if (!self) {
        if (outCount)
            *outCount = 0u;
        return;
    }
    if (outOffsets)
        memcpy(outOffsets, (*self).offsets, (size_t)(*self).stopCount * sizeof(float));
    if (outCount)
        *outCount = (*self).stopCount;
}

;;GETTER
void VectorBrush_getTransform(const VectorBrush *self, float *outM6) {
    if (!self || !outM6)
        return;
    memcpy(outM6, (*self).transform, 6 * sizeof(float));
}
