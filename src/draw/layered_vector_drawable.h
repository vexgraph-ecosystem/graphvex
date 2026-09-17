#ifndef DRAW_LAYERED_VECTOR_DRAWABLE_H
#define DRAW_LAYERED_VECTOR_DRAWABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "draw/drawable.h"
#include "draw/vector_drawable.h"
#include "graphvex/type.h"

// draw/layered_vector_drawable.h — Multi-layer vector board owning N VectorDrawable layers.
//
// A LayeredVectorDrawable is the multi-layer vector board: a flat,
// data-oriented row table (LayeredVectorRow) of owned VectorDrawable layers
// carrying per-row opacity, blend mode, and visibility flag — no fixed layer
// count and no 32-bit bitmask width limit (the Data-Oriented Storage Law +
// the Dynamic Scalability & Anti-Hardcoding Law). Rows are grown by doubling
// and are exposed exclusively through LayeredVectorDrawable_layer* verbs per
// the Sub-Part Field Segregation Law. Renders visible vector layers onto a
// destination raster Drawable.

#ifndef LAYER_BLEND_NORMAL
#define LAYER_BLEND_NORMAL   0u
#define LAYER_BLEND_MULTIPLY 1u
#define LAYER_BLEND_SCREEN   2u
#define LAYER_BLEND_OVERLAY  3u
#endif

// SLOT RECORD — behaviourless per-layer row owned by LayeredVectorDrawable
// (the Single Class Per File Law slot-record exception).
typedef struct LayeredVectorRow {
    VectorDrawable *drawable; // owned layer drawable
    float opacity;            // per-layer opacity [0..1]
    uint32_t blendMode;       // 0=NORMAL, 1=MULTIPLY, 2=SCREEN, 3=OVERLAY
    bool visible;             // per-row visibility (no bitmask width limit)
} LayeredVectorRow;

typedef struct LayeredVectorDrawable {
    // --- LayeredVectorDrawable core ---
    LayeredVectorRow *layers; // flat row table, doubles on demand
    size_t layerCount;        // active rows
    size_t layerCapacity;     // allocated row slots
    uint32_t activeIndex;     // index of active target layer
    uint32_t width;           // board pixel width
    uint32_t height;          // board pixel height
    bool dirty;               // true when any layer mutated or order changed
    uint64_t typeId;          // block-header type id (TYPE_LAYERED_VECTOR_DRAWABLE_SINGLETON)
} LayeredVectorDrawable;

// Constructors
LayeredVectorDrawable *LayeredVectorDrawable_0(void);
LayeredVectorDrawable *LayeredVectorDrawable_2(uint32_t w, uint32_t h);
LayeredVectorDrawable *LayeredVectorDrawable_3(uint32_t w, uint32_t h, size_t initialLayers);

// Core functions
void LayeredVectorDrawable_free(LayeredVectorDrawable *self);
uint32_t LayeredVectorDrawable_addLayer(LayeredVectorDrawable *self);
bool LayeredVectorDrawable_removeLayer(LayeredVectorDrawable *self, uint32_t index);
void LayeredVectorDrawable_render(LayeredVectorDrawable *self, Drawable *dest);
VectorDrawable *LayeredVectorDrawable_activeLayer(LayeredVectorDrawable *self);

// Layer Part Verbs (the Sub-Part Field Segregation Law)
VectorDrawable *LayeredVectorDrawable_layerGet(const LayeredVectorDrawable *self, uint32_t index);
void LayeredVectorDrawable_layerSetOpacity(LayeredVectorDrawable *self, uint32_t index, float opacity);
float LayeredVectorDrawable_layerGetOpacity(const LayeredVectorDrawable *self, uint32_t index);
void LayeredVectorDrawable_layerSetBlend(LayeredVectorDrawable *self, uint32_t index, uint32_t blendMode);
uint32_t LayeredVectorDrawable_layerGetBlend(const LayeredVectorDrawable *self, uint32_t index);
void LayeredVectorDrawable_layerSetVisible(LayeredVectorDrawable *self, uint32_t index, bool visible);
bool LayeredVectorDrawable_layerIsVisible(const LayeredVectorDrawable *self, uint32_t index);

// Owner Mutators/Accessors
// setVisibleMask/getVisibleMask are a 32-row compatibility view (bit i = row i
// visibility); the row flag is the storage and has no width limit.
void LayeredVectorDrawable_setActiveIndex(LayeredVectorDrawable *self, uint32_t activeIndex);
void LayeredVectorDrawable_setVisibleMask(LayeredVectorDrawable *self, uint32_t visibleMask);
void LayeredVectorDrawable_setWidth(LayeredVectorDrawable *self, uint32_t width);
void LayeredVectorDrawable_setHeight(LayeredVectorDrawable *self, uint32_t height);
void LayeredVectorDrawable_setSize(LayeredVectorDrawable *self, uint32_t w, uint32_t h);
void LayeredVectorDrawable_setDirty(LayeredVectorDrawable *self, bool dirty);

uint32_t LayeredVectorDrawable_getActiveIndex(const LayeredVectorDrawable *self);
uint32_t LayeredVectorDrawable_getVisibleMask(const LayeredVectorDrawable *self);
size_t LayeredVectorDrawable_getLayerCount(const LayeredVectorDrawable *self);
bool LayeredVectorDrawable_isDirty(const LayeredVectorDrawable *self);
uint32_t LayeredVectorDrawable_getWidth(const LayeredVectorDrawable *self);
uint32_t LayeredVectorDrawable_getHeight(const LayeredVectorDrawable *self);
uint64_t LayeredVectorDrawable_getTypeId(const LayeredVectorDrawable *self);

#define LayeredVectorDrawable(...) CONSTRUCTOR_DISPATCH(LayeredVectorDrawable, __VA_ARGS__)

#endif
