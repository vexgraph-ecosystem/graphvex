#include "paint/vector_brush.h"

#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VectorBrush (paint/vector_brush.c)
 * LEVEL: L2 — Behavior (gradient brush behavior API)
 * ============================================================================
 * Gradient/pattern brush in vector space (resolution-free): heap-grown
 * stops plus a 6-float affine snap. Embeds Brush first so a VectorBrush
 * upcasts to Brush by address. Tint and master alpha come from the embedded
 * base; pipelines resolve interior color from the stops. The struct rides
 * the vexspoke arena via Memory_alloc(TYPE_VECTOR_BRUSH_SINGLETON) with a
 * calloc fallback for standalone builds.
 *
 * STRUCT FIELDS (Mirroring paint/vector_brush.h):
 * ----------------------------------------------------------------------------
 *   VectorBrush {
 *     // --- VectorBrush core (owner fields: base first for upcast) ---
 *     Brush base;            // embed-first base (color + opacity + typeId)
 *     // --- Stops part (heap-grown stop table) ---
 *     uint32_t *colors;      // packed stop colors (0xAARRGGBB)
 *     float *offsets;        // stop positions [0..1] parallel to colors
 *     uint32_t stopCount;    // live stops in [0..stopCap]
 *     uint32_t stopCap;      // allocated stop slots (INIT 8, doubles on demand)
 *     // --- Transform part (affine snap for gradient space) ---
 *     float transform[6];    // 2D affine [a b c d tx ty], identity default
 *   }
 * PRIVATE HELPERS (file-local, pure-data/behavior, never included):
 * ----------------------------------------------------------------------------
 *   vectorBrushReserveStops(self, need)  // grow both stop rows to fit `need`
 *   vectorBrushInitStops(self, c0, c1)   // seed the two default stops
 *   vectorBrushInitTransform(self)       // seed the identity affine
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - VectorBrush()                : VectorBrush_0()
 *   - VectorBrush(c0, c1)          : VectorBrush_2(c0, c1)
 *
 * Core Functions:
 *   - VectorBrush_free(self)
 *   - VectorBrush_asBrush(self)
 *   - VectorBrush_constBrush(self)
 *
 * Setters:
 *   - VectorBrush_setColor(self, color)
 *   - VectorBrush_setOpacity(self, opacity)
 *   - VectorBrush_setStopCount(self, n)
 *   - VectorBrush_setStop(self, index, color, offset)
 *   - VectorBrush_setColors(colors, count, dest)
 *   - VectorBrush_setOffsets(offsets, count, dest)
 *   - VectorBrush_setTransform(self, m6)
 *   - VectorBrush_setTransformAt(self, i, v)
 *
 * Getters:
 *   - VectorBrush_getColor(self)
 *   - VectorBrush_getOpacity(self)
 *   - VectorBrush_getTypeId(self)
 *   - VectorBrush_getStopCount(self)
 *   - VectorBrush_getStopColor(self, index)
 *   - VectorBrush_getStopOffset(self, index)
 *   - VectorBrush_getTransformAt(self, i)
 *   - VectorBrush_getStop(self, index, outColor, outOffset)
 *   - VectorBrush_getColors(self, outColors, outCount)
 *   - VectorBrush_getOffsets(self, outOffsets, outCount)
 *   - VectorBrush_getTransform(self, outM6)
 * ============================================================================
 */

// paint/vector_brush.c — Gradient/pattern brush implementation.

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

// Reserve room for `need` stops, doubling from VECTOR_BRUSH_STOPS_INIT (the
// Dynamic Scalability & Anti-Hardcoding Law). The two parallel rows grow
// together; a failure after the first row moved keeps that grown buffer and
// leaves the cap unchanged, so the next call simply retries.
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
    // Fresh slots keep the class defaults: color 0 opaque-black, offset 1.
    for (uint32_t i = oldCap; i < newCap; i++) {
        colors[i] = 0u;
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

// CONSTRUCTORS

VectorBrush *VectorBrush_0(void) {
    return VectorBrush_2(0xFF000000u, 0xFFFFFFFFu);
}

VectorBrush *VectorBrush_2(uint32_t c0, uint32_t c1) {
    VectorBrush *self = (VectorBrush*) Memory_alloc(TYPE_VECTOR_BRUSH_SINGLETON, sizeof(VectorBrush));
    if (!self)
        self = (VectorBrush*) calloc(1, sizeof(VectorBrush));
    if (!self)
        return nullptr;
    Brush *b = &(*self).base;
    (*b).color = 0xFF000000u;
    (*b).opacity = 1.0f;
    (*b).typeId = TYPE_VECTOR_BRUSH_SINGLETON;
    vectorBrushInitStops(self, c0, c1);
    vectorBrushInitTransform(self);
    return self;
}

// CORE FUNCTIONS

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

// SETTERS

void VectorBrush_setColor(VectorBrush *self, uint32_t color) {
    if (!self)
        return;
    Brush *b = &(*self).base;
    (*b).color = color;
}

void VectorBrush_setOpacity(VectorBrush *self, float opacity) {
    if (!self)
        return;
    Brush *b = &(*self).base;
    (*b).opacity = opacity;
}

void VectorBrush_setStopCount(VectorBrush *self, uint32_t n) {
    if (!self)
        return;
    if (n == 0u) {
        (*self).stopCount = 0u;
        return;
    }
    if (!vectorBrushReserveStops(self, n))
        return; // OOM: current stops kept (drop-degrade)
    (*self).stopCount = n;
}

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

void VectorBrush_setColors(const uint32_t *colors, uint32_t count, VectorBrush *dest) {
    if (!colors || !dest)
        return;
    if (count == 0u || !vectorBrushReserveStops(dest, count))
        return; // OOM: current stops kept (drop-degrade)
    memcpy((*dest).colors, colors, (size_t)count * sizeof(uint32_t));
    (*dest).stopCount = count;
}

void VectorBrush_setOffsets(const float *offsets, uint32_t count, VectorBrush *dest) {
    if (!offsets || !dest)
        return;
    if (count == 0u || !vectorBrushReserveStops(dest, count))
        return; // OOM: current offsets kept (drop-degrade)
    memcpy((*dest).offsets, offsets, (size_t)count * sizeof(float));
    if ((*dest).stopCount < count)
        (*dest).stopCount = count;
}

void VectorBrush_setTransform(VectorBrush *self, const float *m6) {
    if (!self || !m6)
        return;
    memcpy((*self).transform, m6, 6 * sizeof(float));
}

void VectorBrush_setTransformAt(VectorBrush *self, uint32_t i, float v) {
    if (!self)
        return;
    if (i >= 6)
        return;
    (*self).transform[i] = v;
}

// GETTERS

uint32_t VectorBrush_getColor(const VectorBrush *self) {
    if (!self)
        return 0u;
    const Brush *b = &(*self).base;
    return (*b).color;
}

float VectorBrush_getOpacity(const VectorBrush *self) {
    if (!self)
        return 0.0f;
    const Brush *b = &(*self).base;
    return (*b).opacity;
}

uint64_t VectorBrush_getTypeId(const VectorBrush *self) {
    if (!self)
        return 0;
    const Brush *b = &(*self).base;
    return (*b).typeId;
}

uint32_t VectorBrush_getStopCount(const VectorBrush *self) {
    return self ? (*self).stopCount : 0u;
}

uint32_t VectorBrush_getStopColor(const VectorBrush *self, uint32_t index) {
    if (!self || index >= (*self).stopCap)
        return 0u;
    return (*self).colors[index];
}

float VectorBrush_getStopOffset(const VectorBrush *self, uint32_t index) {
    if (!self || index >= (*self).stopCap)
        return 0.0f;
    return (*self).offsets[index];
}

float VectorBrush_getTransformAt(const VectorBrush *self, uint32_t i) {
    if (!self || i >= 6)
        return 0.0f;
    return (*self).transform[i];
}

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

void VectorBrush_getTransform(const VectorBrush *self, float *outM6) {
    if (!self || !outM6)
        return;
    memcpy(outM6, (*self).transform, 6 * sizeof(float));
}
