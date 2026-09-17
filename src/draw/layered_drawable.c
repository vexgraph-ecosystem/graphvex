#include "draw/layered_drawable.h"

#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/overview.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: LayeredDrawable (draw/layered_drawable.c)
 * LEVEL: L3 — Module Code (multi-layer raster board behavior)
 * ============================================================================
 * N Drawable layers in one flat, data-oriented row table + active index.
 * Rows double on demand (no layer ceiling, no 32-bit visibility width limit).
 * CPU stub: records layer mutations and marks dirty without rasterizing on
 * GPU. Exposes layers exclusively via the Sub-Part Field Segregation Law
 * LayeredDrawable_layer* verbs.
 *
 * STRUCT FIELDS (Mirroring draw/layered_drawable.h):
 * ----------------------------------------------------------------------------
 *   LayeredRow {           // SLOT RECORD: behaviourless per-layer row
 *     Drawable *drawable;  // owned layer drawable
 *     float opacity;       // per-layer opacity [0..1]
 *     uint32_t blendMode;  // 0=NORMAL, 1=MULTIPLY, 2=SCREEN, 3=OVERLAY
 *     bool visible;        // per-row visibility
 *   }
 *   LayeredDrawable {
 *     // --- LayeredDrawable core ---
 *     LayeredRow *layers;   // flat row table, doubles on demand
 *     size_t layerCount;    // active rows
 *     size_t layerCapacity; // allocated row slots
 *     uint32_t activeIndex; // index of active target layer
 *     uint32_t width;       // board pixel width
 *     uint32_t height;      // board pixel height
 *     uint64_t typeId;      // block-header type id
 *     bool dirty;           // true when any layer mutated or order changed
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - LayeredDrawable()              : LayeredDrawable_0()
 *   - LayeredDrawable(w, h)          : LayeredDrawable_2(w, h)
 *   - LayeredDrawable(w, h, layers)  : LayeredDrawable_3(w, h, layers)
 *
 * Core Functions:
 *   - LayeredDrawable_free(self)
 *   - LayeredDrawable_addLayer(self)
 *   - LayeredDrawable_removeLayer(self, index)
 *   - LayeredDrawable_composite(self, dest)
 *   - LayeredDrawable_activeLayer(self)
 *   - LayeredDrawable_layerGet(self, index)
 *   - LayeredDrawable_layerSetOpacity(self, index, opacity)
 *   - LayeredDrawable_layerGetOpacity(self, index)
 *   - LayeredDrawable_layerSetBlend(self, index, blendMode)
 *   - LayeredDrawable_layerGetBlend(self, index)
 *   - LayeredDrawable_layerSetVisible(self, index, visible)
 *   - LayeredDrawable_layerIsVisible(self, index)
 *
 * Setters:
 *   - LayeredDrawable_setActiveIndex(self, activeIndex)
 *   - LayeredDrawable_setVisibleMask(self, visibleMask)
 *   - LayeredDrawable_setWidth(self, width)
 *   - LayeredDrawable_setHeight(self, height)
 *   - LayeredDrawable_setSize(self, w, h)
 *   - LayeredDrawable_setDirty(self, dirty)
 *
 * Getters:
 *   - LayeredDrawable_getActiveIndex(self)
 *   - LayeredDrawable_getVisibleMask(self)
 *   - LayeredDrawable_getLayerCount(self)
 *   - LayeredDrawable_isDirty(self)
 *   - LayeredDrawable_getWidth(self)
 *   - LayeredDrawable_getHeight(self)
 *   - LayeredDrawable_getTypeId(self)
 * ============================================================================
 */

// draw/layered_drawable.c — Multi-layer raster board implementation (CPU stub).

static void layeredDrawableFreeStorage(LayeredDrawable *self) {
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

static LayeredDrawable *layeredDrawableCreate(uint32_t w, uint32_t h, size_t initialLayers) {
    if (w == 0 || h == 0)
        return nullptr;
    if (initialLayers == 0)
        initialLayers = 1;

    LayeredDrawable *self = (LayeredDrawable*) Memory_alloc(TYPE_LAYERED_DRAWABLE_SINGLETON, sizeof(LayeredDrawable));
    if (!self)
        self = (LayeredDrawable*) calloc(1, sizeof(LayeredDrawable));
    if (!self)
        return nullptr;

    size_t cap = 4;
    while (cap < initialLayers)
        cap *= 2;

    LayeredRow *rows = (LayeredRow*) calloc(cap, sizeof(LayeredRow));
    if (!rows) {
        layeredDrawableFreeStorage(self);
        return nullptr;
    }

    size_t built = 0;
    for (; built < initialLayers; built++) {
        Drawable *d = Drawable_2(w, h);
        if (!d) {
            for (size_t j = 0; j < built; j++)
                Drawable_free(rows[j].drawable);
            free(rows);
            layeredDrawableFreeStorage(self);
            return nullptr;
        }
        rows[built].drawable = d;
        rows[built].opacity = 1.0f;
        rows[built].blendMode = LAYER_BLEND_NORMAL;
        rows[built].visible = true;
    }

    (*self).layers = rows;
    (*self).layerCount = initialLayers;
    (*self).layerCapacity = cap;
    (*self).activeIndex = 0;
    (*self).width = w;
    (*self).height = h;
    (*self).typeId = TYPE_LAYERED_DRAWABLE_SINGLETON;
    (*self).dirty = false;

    return self;
}

// CONSTRUCTORS
LayeredDrawable *LayeredDrawable_0() {
    return layeredDrawableCreate(1, 1, 1);
}

LayeredDrawable *LayeredDrawable_2(uint32_t w, uint32_t h) {
    return layeredDrawableCreate(w, h, 1);
}

LayeredDrawable *LayeredDrawable_3(uint32_t w, uint32_t h, size_t initialLayers) {
    return layeredDrawableCreate(w, h, initialLayers);
}

// CORE FUNCTIONS
void LayeredDrawable_free(LayeredDrawable *self) {
    if (!self)
        return;
    if ((*self).layers) {
        for (size_t i = 0; i < (*self).layerCount; i++) {
            Drawable_free((*self).layers[i].drawable);
            (*self).layers[i].drawable = nullptr;
        }
        free((*self).layers);
        (*self).layers = nullptr;
    }
    layeredDrawableFreeStorage(self);
}

uint32_t LayeredDrawable_addLayer(LayeredDrawable *self) {
    if (!self)
        return UINT32_MAX;

    size_t count = (*self).layerCount;
    size_t cap = (*self).layerCapacity;
    if (count >= cap) {
        size_t newCap = cap == 0 ? 4 : cap * 2;
        LayeredRow *newRows = (LayeredRow*) realloc((*self).layers, newCap * sizeof(LayeredRow));
        if (!newRows)
            return UINT32_MAX;
        memset(newRows + cap, 0, (newCap - cap) * sizeof(LayeredRow));
        (*self).layers = newRows;
        (*self).layerCapacity = newCap;
    }

    Drawable *d = Drawable_2((*self).width, (*self).height);
    if (!d)
        return UINT32_MAX;

    uint32_t index = (uint32_t) count;
    LayeredRow *row = &(*self).layers[index];
    (*row).drawable = d;
    (*row).opacity = 1.0f;
    (*row).blendMode = LAYER_BLEND_NORMAL;
    (*row).visible = true;
    (*self).layerCount = count + 1;
    (*self).activeIndex = index;
    (*self).dirty = true;

    return index;
}

bool LayeredDrawable_removeLayer(LayeredDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return false;

    LayeredRow *rows = (*self).layers;
    Drawable_free(rows[index].drawable);

    size_t moveCount = (*self).layerCount - index - 1;
    if (moveCount > 0)
        memmove(&rows[index], &rows[index + 1], moveCount * sizeof(LayeredRow));
    size_t tail = (*self).layerCount - 1;
    memset(&rows[tail], 0, sizeof(LayeredRow));

    (*self).layerCount = tail;
    if ((*self).activeIndex >= (*self).layerCount)
        (*self).activeIndex = ((*self).layerCount > 0) ? (uint32_t)((*self).layerCount - 1) : 0;

    (*self).dirty = true;
    return true;
}

void LayeredDrawable_composite(LayeredDrawable *self, Drawable *dest) {
    if (!self || !dest)
        return;
    (void)self;
    Drawable_setDirty(dest, true);
}

Drawable *LayeredDrawable_activeLayer(LayeredDrawable *self) {
    if (!self || (*self).layerCount == 0)
        return nullptr;
    if ((*self).activeIndex >= (*self).layerCount)
        return nullptr;
    return (*self).layers[(*self).activeIndex].drawable;
}

Drawable *LayeredDrawable_layerGet(const LayeredDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return nullptr;
    return (*self).layers[index].drawable;
}

void LayeredDrawable_layerSetOpacity(LayeredDrawable *self, uint32_t index, float opacity) {
    if (!self || index >= (*self).layerCount)
        return;
    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    (*self).layers[index].opacity = opacity;
    (*self).dirty = true;
}

float LayeredDrawable_layerGetOpacity(const LayeredDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return 0.0f;
    return (*self).layers[index].opacity;
}

void LayeredDrawable_layerSetBlend(LayeredDrawable *self, uint32_t index, uint32_t blendMode) {
    if (!self || index >= (*self).layerCount)
        return;
    (*self).layers[index].blendMode = blendMode;
    (*self).dirty = true;
}

uint32_t LayeredDrawable_layerGetBlend(const LayeredDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return 0;
    return (*self).layers[index].blendMode;
}

void LayeredDrawable_layerSetVisible(LayeredDrawable *self, uint32_t index, bool visible) {
    if (!self || index >= (*self).layerCount)
        return;
    (*self).layers[index].visible = visible;
    (*self).dirty = true;
}

bool LayeredDrawable_layerIsVisible(const LayeredDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return false;
    return (*self).layers[index].visible;
}

// SETTERS
void LayeredDrawable_setActiveIndex(LayeredDrawable *self, uint32_t activeIndex) {
    if (!self)
        return;
    if (activeIndex < (*self).layerCount || (activeIndex == 0 && (*self).layerCount == 0))
        (*self).activeIndex = activeIndex;
}

void LayeredDrawable_setVisibleMask(LayeredDrawable *self, uint32_t visibleMask) {
    if (!self)
        return;
    size_t n = (*self).layerCount;
    if (n > 32u)
        n = 32u;
    for (size_t i = 0; i < n; i++)
        (*self).layers[i].visible = ((visibleMask & (1u << i)) != 0);
    (*self).dirty = true;
}

void LayeredDrawable_setWidth(LayeredDrawable *self, uint32_t width) {
    if (!self)
        return;
    (*self).width = width;
    (*self).dirty = true;
}

void LayeredDrawable_setHeight(LayeredDrawable *self, uint32_t height) {
    if (!self)
        return;
    (*self).height = height;
    (*self).dirty = true;
}

void LayeredDrawable_setSize(LayeredDrawable *self, uint32_t w, uint32_t h) {
    if (!self)
        return;
    (*self).width = w;
    (*self).height = h;
    (*self).dirty = true;
}

void LayeredDrawable_setDirty(LayeredDrawable *self, bool dirty) {
    if (!self)
        return;
    (*self).dirty = dirty;
}

// GETTERS
uint32_t LayeredDrawable_getActiveIndex(const LayeredDrawable *self) {
    return self ? (*self).activeIndex : 0;
}

uint32_t LayeredDrawable_getVisibleMask(const LayeredDrawable *self) {
    if (!self)
        return 0;
    uint32_t mask = 0;
    size_t n = (*self).layerCount;
    if (n > 32u)
        n = 32u;
    for (size_t i = 0; i < n; i++)
        if ((*self).layers[i].visible)
            mask |= (1u << i);
    return mask;
}

size_t LayeredDrawable_getLayerCount(const LayeredDrawable *self) {
    return self ? (*self).layerCount : 0;
}

bool LayeredDrawable_isDirty(const LayeredDrawable *self) {
    return self ? (*self).dirty : false;
}

uint32_t LayeredDrawable_getWidth(const LayeredDrawable *self) {
    return self ? (*self).width : 0;
}

uint32_t LayeredDrawable_getHeight(const LayeredDrawable *self) {
    return self ? (*self).height : 0;
}

uint64_t LayeredDrawable_getTypeId(const LayeredDrawable *self) {
    return self ? (*self).typeId : 0;
}
