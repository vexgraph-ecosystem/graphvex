#include "paint/raster_brush.h"

#include <math.h>
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
 * DEFINITION: RasterBrush
 * ============================================================================
 * Texture and procedural stamp brush extending the base Brush primitive with
 * an embedded 32x32 R8 signed-distance field (SDF) tip mask and per-dab dynamics
 * (diameter, stamp alpha, spacing, jitter, and edge softness).
 *
 * Encapsulates base pigmentation formatted strictly as 0xRRGGBBAA under the Strict
 * 0xRRGGBBAA Color Law. Embedded tips are evaluated on the CPU during creation
 * without dynamic shader compilation or GPU round-trips. An optional borrowed
 * Image handle allows caller-specified textured stamp tips. Struct memory is
 * managed via the vexspoke typed memory arena (TYPE_RASTER_BRUSH_SINGLETON)
 * with a calloc fallback for standalone targets.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: RasterBrush (paint/raster_brush.c)
 * LEVEL: L2 — Behavior (stamp brush behavior API)
 * ============================================================================
 * SUMMARY:
 *   Stamp brush combining an embedded 32x32 R8 SDF tip mask with per-dab
 *   dynamics and 0xRRGGBBAA pigmentation. Embeds Brush at offset 0.
 *
 * STRUCT FIELDS (Mirroring paint/raster_brush.h):
 * ----------------------------------------------------------------------------
 *   Brush base;              // embed-first base (color + opacity + typeId)
 *   uint8_t tip[1024];       // 32x32 R8 mask bytes, row-major, 255 = ink
 *   float size;              // stamp diameter in pixels (> 0)
 *   float opacity;           // per-dab stamp alpha [0..1], mirrors base opacity
 *   float spacing;           // dab advance as fraction of size (0.25 = smooth)
 *   float jitter;            // positional jitter amount [0..1] (0 = off)
 *   float softness;          // stamping edge softness [0..1]
 *   Image *tipImage;         // borrowed textured tip (null = procedural); never freed
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - RasterBrush_0(void)                                : Default brush (round tip, size 16, 0x000000FF)
 *   - RasterBrush_1(preset)                              : Preset brush (ROUND / SOFT / SQUARE)
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - RasterBrush_free(self)                             : Release brush struct memory
 *   - RasterBrush_makeRound(dest)                        : Bake procedural round tip into mask
 *   - RasterBrush_makeSoft(dest)                         : Bake procedural soft-edged tip into mask
 *   - RasterBrush_makeSquare(dest)                       : Bake procedural square tip into mask
 *   - RasterBrush_asBrush(self)                          : Borrow base Brush pointer
 *   - RasterBrush_constBrush(self)                       : Borrow const base Brush pointer
 *
 * Private Core Functions: (.c static)
 *   - rasterBrushFreeStorage(self)                       : Deallocate arena or heap storage
 *   - rasterBrushInitScalars(self)                       : Seed default dynamics and 0xRRGGBBAA color
 *   - edgeToAlpha(t)                                     : Antialiased distance-to-alpha helper
 *   - bakeRound(dest)                                    : Inner round tip distance field calculation
 *   - bakeSoft(dest)                                     : Inner soft tip distance field calculation
 *   - bakeSquare(dest)                                   : Inner square tip distance field calculation
 *
 * Public Setters: (.h)
 *   - RasterBrush_setColor(self, color)                  : Mutate 0xRRGGBBAA base pigmentation
 *   - RasterBrush_setSize(self, size)                    : Mutate stamp diameter
 *   - RasterBrush_setOpacity(self, opacity)              : Mutate stamp and base opacity
 *   - RasterBrush_setSpacing(self, spacing)              : Mutate dab advance fraction
 *   - RasterBrush_setJitter(self, jitter)                : Mutate positional jitter amount
 *   - RasterBrush_setSoftness(self, softness)            : Mutate edge softness factor
 *   - RasterBrush_setTipPixel(self, x, y, v)             : Mutate single mask pixel
 *   - RasterBrush_setTip(src1024, dest)                  : Bulk copy mask bytes
 *   - RasterBrush_setTipImage(self, img)                 : Bind borrowed textured tip image
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - RasterBrush_getColor(self)                         : Query 0xRRGGBBAA base color
 *   - RasterBrush_getTypeId(self)                        : Query type identity stamp
 *   - RasterBrush_getSize(self)                          : Query stamp diameter
 *   - RasterBrush_getOpacity(self)                       : Query stamp opacity
 *   - RasterBrush_getSpacing(self)                       : Query dab advance fraction
 *   - RasterBrush_getJitter(self)                        : Query positional jitter amount
 *   - RasterBrush_getSoftness(self)                      : Query edge softness factor
 *   - RasterBrush_getTipPixel(self, x, y)                : Query single mask pixel
 *   - RasterBrush_tip(self)                              : Query mutable tip mask pointer
 *   - RasterBrush_constTip(self)                         : Query const tip mask pointer
 *   - RasterBrush_getTipImage(self)                      : Query bound tip image
 *   - RasterBrush_getTip(self, out1024)                  : Bulk read mask bytes
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

static void rasterBrushFreeStorage(RasterBrush *self);
static void rasterBrushInitScalars(RasterBrush *self);
static uint8_t edgeToAlpha(float t);
static void bakeRound(RasterBrush *dest);
static void bakeSoft(RasterBrush *dest);
static void bakeSquare(RasterBrush *dest);

RasterBrush *RasterBrush_0(void) {
    return RasterBrush_1(RASTER_BRUSH_ROUND);
}

RasterBrush *RasterBrush_1(uint32_t preset) {
    RasterBrush *self = (RasterBrush*) Memory_alloc(TYPE_RASTER_BRUSH_SINGLETON, sizeof(RasterBrush));
    if (!self)
        self = (RasterBrush*) calloc(1, sizeof(RasterBrush));
    if (!self)
        return nullptr;
    rasterBrushInitScalars(self);
    if (preset == RASTER_BRUSH_SOFT)
        bakeSoft(self);
    else if (preset == RASTER_BRUSH_SQUARE)
        bakeSquare(self);
    else
        bakeRound(self);
    return self;
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

static void rasterBrushFreeStorage(RasterBrush *self) {
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

static void rasterBrushInitScalars(RasterBrush *self) {
    Brush *b = &(*self).base;
    (*b).color = 0x000000FFu;
    (*b).opacity = 1.0f;
    (*b).typeId = TYPE_RASTER_BRUSH_SINGLETON;
    (*self).size = 16.0f;
    (*self).opacity = 1.0f;
    (*self).spacing = 0.25f;
    (*self).jitter = 0.0f;
    (*self).softness = 0.5f;
    (*self).tipImage = nullptr;
}

static uint8_t edgeToAlpha(float t) {
    if (t >= 0.5f)
        return 255u;
    if (t <= -0.5f)
        return 0u;
    return (uint8_t) ((t + 0.5f) * 255.0f);
}

static void bakeRound(RasterBrush *dest) {
    float cx = 15.5f;
    float cy = 15.5f;
    float radius = 15.0f;
    for (uint32_t y = 0; y < RASTER_BRUSH_TIP_DIM; y++) {
        for (uint32_t x = 0; x < RASTER_BRUSH_TIP_DIM; x++) {
            float dx = (float) x - cx;
            float dy = (float) y - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            float t = radius - dist;
            (*dest).tip[y * RASTER_BRUSH_TIP_DIM + x] = edgeToAlpha(t);
        }
    }
}

static void bakeSoft(RasterBrush *dest) {
    float cx = 15.5f;
    float cy = 15.5f;
    float radius = 15.0f;
    for (uint32_t y = 0; y < RASTER_BRUSH_TIP_DIM; y++) {
        for (uint32_t x = 0; x < RASTER_BRUSH_TIP_DIM; x++) {
            float dx = (float) x - cx;
            float dy = (float) y - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            float d = dist / radius;
            float f = 1.0f - d;
            if (f < 0.0f)
                f = 0.0f;
            f = f * f;
            (*dest).tip[y * RASTER_BRUSH_TIP_DIM + x] = (uint8_t) (f * 255.0f);
        }
    }
}

static void bakeSquare(RasterBrush *dest) {
    float cx = 15.5f;
    float cy = 15.5f;
    float half = 15.0f;
    for (uint32_t y = 0; y < RASTER_BRUSH_TIP_DIM; y++) {
        for (uint32_t x = 0; x < RASTER_BRUSH_TIP_DIM; x++) {
            float dx = fabsf((float) x - cx);
            float dy = fabsf((float) y - cy);
            float d = dx > dy ? dx : dy;
            float t = half - d;
            (*dest).tip[y * RASTER_BRUSH_TIP_DIM + x] = edgeToAlpha(t);
        }
    }
}

void RasterBrush_free(RasterBrush *self) {
    if (!self)
        return;
    (*self).tipImage = nullptr;
    rasterBrushFreeStorage(self);
}

void RasterBrush_makeRound(RasterBrush *dest) {
    if (!dest)
        return;
    bakeRound(dest);
}

void RasterBrush_makeSoft(RasterBrush *dest) {
    if (!dest)
        return;
    bakeSoft(dest);
}

void RasterBrush_makeSquare(RasterBrush *dest) {
    if (!dest)
        return;
    bakeSquare(dest);
}

Brush *RasterBrush_asBrush(RasterBrush *self) {
    return (Brush*) self;
}

const Brush *RasterBrush_constBrush(const RasterBrush *self) {
    return (const Brush*) self;
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void RasterBrush_setColor(RasterBrush *self, uint32_t color) {
    if (!self)
        return;
    Brush *b = &(*self).base;
    (*b).color = color;
}

;;SETTER
void RasterBrush_setSize(RasterBrush *self, float size) {
    if (!self)
        return;
    (*self).size = size;
}

;;SETTER
void RasterBrush_setOpacity(RasterBrush *self, float opacity) {
    if (!self)
        return;
    (*self).opacity = opacity;
    Brush *b = &(*self).base;
    (*b).opacity = opacity;
}

;;SETTER
void RasterBrush_setSpacing(RasterBrush *self, float spacing) {
    if (!self)
        return;
    (*self).spacing = spacing;
}

;;SETTER
void RasterBrush_setJitter(RasterBrush *self, float jitter) {
    if (!self)
        return;
    (*self).jitter = jitter;
}

;;SETTER
void RasterBrush_setSoftness(RasterBrush *self, float softness) {
    if (!self)
        return;
    (*self).softness = softness;
}

;;SETTER
void RasterBrush_setTipPixel(RasterBrush *self, uint32_t x, uint32_t y, uint8_t v) {
    if (!self)
        return;
    if (x >= RASTER_BRUSH_TIP_DIM || y >= RASTER_BRUSH_TIP_DIM)
        return;
    (*self).tip[y * RASTER_BRUSH_TIP_DIM + x] = v;
}

;;SETTER
void RasterBrush_setTip(const uint8_t *src1024, RasterBrush *dest) {
    if (!src1024 || !dest)
        return;
    memcpy((*dest).tip, src1024, RASTER_BRUSH_TIP_BYTES);
}

;;SETTER
void RasterBrush_setTipImage(RasterBrush *self, Image *img) {
    if (!self)
        return;
    (*self).tipImage = img;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t RasterBrush_getColor(const RasterBrush *self) {
    if (!self)
        return 0u;
    const Brush *b = &(*self).base;
    return (*b).color;
}

;;GETTER
uint64_t RasterBrush_getTypeId(const RasterBrush *self) {
    if (!self)
        return 0;
    const Brush *b = &(*self).base;
    return (*b).typeId;
}

;;GETTER
float RasterBrush_getSize(const RasterBrush *self) {
    return self ? (*self).size : 0.0f;
}

;;GETTER
float RasterBrush_getOpacity(const RasterBrush *self) {
    return self ? (*self).opacity : 0.0f;
}

;;GETTER
float RasterBrush_getSpacing(const RasterBrush *self) {
    return self ? (*self).spacing : 0.0f;
}

;;GETTER
float RasterBrush_getJitter(const RasterBrush *self) {
    return self ? (*self).jitter : 0.0f;
}

;;GETTER
float RasterBrush_getSoftness(const RasterBrush *self) {
    return self ? (*self).softness : 0.0f;
}

;;GETTER
uint8_t RasterBrush_getTipPixel(const RasterBrush *self, uint32_t x, uint32_t y) {
    if (!self || x >= RASTER_BRUSH_TIP_DIM || y >= RASTER_BRUSH_TIP_DIM)
        return 0u;
    return (*self).tip[y * RASTER_BRUSH_TIP_DIM + x];
}

;;GETTER
uint8_t *RasterBrush_tip(RasterBrush *self) {
    if (!self)
        return nullptr;
    return (*self).tip;
}

;;GETTER
const uint8_t *RasterBrush_constTip(const RasterBrush *self) {
    if (!self)
        return nullptr;
    return (*self).tip;
}

;;GETTER
Image *RasterBrush_getTipImage(const RasterBrush *self) {
    return self ? (*self).tipImage : nullptr;
}

;;GETTER
void RasterBrush_getTip(const RasterBrush *self, uint8_t *out1024) {
    if (!self || !out1024)
        return;
    memcpy(out1024, (*self).tip, RASTER_BRUSH_TIP_BYTES);
}
