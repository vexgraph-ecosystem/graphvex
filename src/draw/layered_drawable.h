#ifndef DRAW_LAYERED_DRAWABLE_H
#define DRAW_LAYERED_DRAWABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "draw/drawable.h"
#include "graphvex/type.h"

// draw/layered_drawable.h — Multi-layer raster board owning N Drawable layers (Ibis).
//
// A LayeredDrawable is the multi-layer raster board: an array of owned
// Drawable layers with per-layer opacity, blend mode, and a 32-bit visibility
// bitmask. Layers are exposed exclusively through LayeredDrawable_layer* verbs
// per the Sub-Part Field Segregation Law. CPU stub: records layer mutations and dirty flag without GPU rasterization.

#define LAYER_BLEND_NORMAL   0u
#define LAYER_BLEND_MULTIPLY 1u
#define LAYER_BLEND_SCREEN   2u
#define LAYER_BLEND_OVERLAY  3u

#define LAYER_MAX_COUNT 32u

typedef struct LayeredDrawable {
    // --- LayeredDrawable core ---
    Drawable **layers;    // array of owned Drawable* layers
    float *opacities;     // per-layer opacity [0..1]
    uint32_t *blendModes; // per-layer blend mode (0=NORMAL, 1=MULTIPLY, 2=SCREEN, 3=OVERLAY)
    size_t layerCount;
    size_t layerCapacity;
    uint32_t activeIndex;
    uint32_t visibleMask;
    uint32_t width;
    uint32_t height;
    uint64_t typeId;
    bool dirty;
} LayeredDrawable;

// Constructors
LayeredDrawable *LayeredDrawable_0(void);
LayeredDrawable *LayeredDrawable_2(uint32_t w, uint32_t h);
LayeredDrawable *LayeredDrawable_3(uint32_t w, uint32_t h, size_t initialLayers);

// Core functions
void LayeredDrawable_free(LayeredDrawable *self);
uint32_t LayeredDrawable_addLayer(LayeredDrawable *self);
bool LayeredDrawable_removeLayer(LayeredDrawable *self, uint32_t index);
void LayeredDrawable_composite(LayeredDrawable *self, Drawable *dest);
Drawable *LayeredDrawable_activeLayer(LayeredDrawable *self);

// Layer Part Verbs (the Sub-Part Field Segregation Law)
Drawable *LayeredDrawable_layerGet(const LayeredDrawable *self, uint32_t index);
void LayeredDrawable_layerSetOpacity(LayeredDrawable *self, uint32_t index, float opacity);
float LayeredDrawable_layerGetOpacity(const LayeredDrawable *self, uint32_t index);
void LayeredDrawable_layerSetBlend(LayeredDrawable *self, uint32_t index, uint32_t blendMode);
uint32_t LayeredDrawable_layerGetBlend(const LayeredDrawable *self, uint32_t index);
void LayeredDrawable_layerSetVisible(LayeredDrawable *self, uint32_t index, bool visible);
bool LayeredDrawable_layerIsVisible(const LayeredDrawable *self, uint32_t index);

// Owner Mutators/Accessors
void LayeredDrawable_setActiveIndex(LayeredDrawable *self, uint32_t activeIndex);
void LayeredDrawable_setVisibleMask(LayeredDrawable *self, uint32_t visibleMask);
void LayeredDrawable_setWidth(LayeredDrawable *self, uint32_t width);
void LayeredDrawable_setHeight(LayeredDrawable *self, uint32_t height);
void LayeredDrawable_setSize(LayeredDrawable *self, uint32_t w, uint32_t h);
void LayeredDrawable_setDirty(LayeredDrawable *self, bool dirty);

uint32_t LayeredDrawable_getActiveIndex(const LayeredDrawable *self);
uint32_t LayeredDrawable_getVisibleMask(const LayeredDrawable *self);
size_t LayeredDrawable_getLayerCount(const LayeredDrawable *self);
bool LayeredDrawable_isDirty(const LayeredDrawable *self);
uint32_t LayeredDrawable_getWidth(const LayeredDrawable *self);
uint32_t LayeredDrawable_getHeight(const LayeredDrawable *self);
uint64_t LayeredDrawable_getTypeId(const LayeredDrawable *self);

#define LayeredDrawable(...) CONSTRUCTOR_DISPATCH(LayeredDrawable, __VA_ARGS__)

#endif
