#include "draw/layered_vector_drawable.h"

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
 * DEFINITION: LayeredVectorDrawable
 * ============================================================================
 * Multi-layer vector drawing board managing an ordered dynamic array of VectorDrawable
 * layer rows. Encapsulates independent vector stream layers with individual opacity,
 * Porter-Duff blend modes, visibility masking, and active layer focus.
 *
 * Composites visible vector layers into a destination raster Drawable board on demand
 * while enforcing data-oriented row table reallocation and sub-part field segregation
 * in compliance with the Unified Graphics Abstraction Law and the Dynamic Scalability & Anti-Hardcoding Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: LayeredVectorDrawable (draw/layered_vector_drawable.c)
 * LEVEL: L3 — Module Code (multi-layer vector board behavior)
 * ============================================================================
 * Multi-layer vector board: one flat, data-oriented row table of N
 * VectorDrawable layers + active index. Rows double on demand (no layer
 * ceiling, no 32-bit visibility width limit). Exposes layers exclusively via
 * the Sub-Part Field Segregation Law LayeredVectorDrawable_layer* verbs.
 * Renders visible layers to a destination raster Drawable board.
 *
 * STRUCT FIELDS (Mirroring draw/layered_vector_drawable.h):
 * ----------------------------------------------------------------------------
 *   LayeredVectorRow {          // SLOT RECORD: behaviourless per-layer row
 *     VectorDrawable *drawable; // owned layer drawable
 *     float opacity;            // per-layer opacity [0..1]
 *     uint32_t blendMode;       // 0=NORMAL, 1=MULTIPLY, 2=SCREEN, 3=OVERLAY
 *     bool visible;             // per-row visibility
 *   }
 *   LayeredVectorDrawable {
 *     // --- LayeredVectorDrawable core ---
 *     LayeredVectorRow *layers; // flat row table, doubles on demand
 *     size_t layerCount;        // active rows
 *     size_t layerCapacity;     // allocated row slots
 *     uint32_t activeIndex;     // index of active target layer
 *     uint32_t width;           // board pixel width
 *     uint32_t height;          // board pixel height
 *     bool dirty;               // true when any layer mutated or order changed
 *     uint64_t typeId;          // block-header type id
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - LayeredVectorDrawable_0(void)                         : Default 1x1 1-layer board
 *   - LayeredVectorDrawable_2(w, h)                         : Sized 1-layer board
 *   - LayeredVectorDrawable_3(w, h, initialLayers)          : Sized N-layer board
 *
 * Private Constructors: (.c static)
 *   - layeredVectorDrawableCreate(w, h, initialLayers)      : Allocate and initialize board
 *
 * Public Core Functions: (.h)
 *   - LayeredVectorDrawable_free(self)                      : Release board and layer rows
 *   - LayeredVectorDrawable_addLayer(self)                  : Append new layer row
 *   - LayeredVectorDrawable_removeLayer(self, index)        : Remove layer row by index
 *   - LayeredVectorDrawable_render(self, dest)              : Render visible layers to raster board
 *   - LayeredVectorDrawable_activeLayer(self)               : Pointer to active target layer
 *   - LayeredVectorDrawable_layerGet(self, index)           : Pointer to layer at index
 *   - LayeredVectorDrawable_layerSetOpacity(self, idx, op)  : Set layer opacity [0..1]
 *   - LayeredVectorDrawable_layerGetOpacity(self, idx)      : Query layer opacity
 *   - LayeredVectorDrawable_layerSetBlend(self, idx, mode)  : Set layer blend mode
 *   - LayeredVectorDrawable_layerGetBlend(self, idx)        : Query layer blend mode
 *   - LayeredVectorDrawable_layerSetVisible(self, idx, vis) : Set layer visibility
 *   - LayeredVectorDrawable_layerIsVisible(self, idx)       : Query layer visibility
 *
 * Private Core Functions: (.c static)
 *   - layeredVectorDrawableFreeStorage(self)                : Deallocate heap memory
 *
 * Public Setters: (.h)
 *   - LayeredVectorDrawable_setActiveIndex(self, idx)       : Set active target layer index
 *   - LayeredVectorDrawable_setVisibleMask(self, mask)      : Set 32-bit visibility bitmask
 *   - LayeredVectorDrawable_setWidth(self, width)           : Set board width
 *   - LayeredVectorDrawable_setHeight(self, height)         : Set board height
 *   - LayeredVectorDrawable_setSize(self, w, h)             : Set board dimensions
 *   - LayeredVectorDrawable_setDirty(self, dirty)           : Set board dirty flag
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - LayeredVectorDrawable_getActiveIndex(self)            : Query active target layer index
 *   - LayeredVectorDrawable_getVisibleMask(self)            : Query 32-bit visibility bitmask
 *   - LayeredVectorDrawable_getLayerCount(self)             : Query total layer row count
 *   - LayeredVectorDrawable_isDirty(self)                   : Query board dirty flag
 *   - LayeredVectorDrawable_getWidth(self)                  : Query board pixel width
 *   - LayeredVectorDrawable_getHeight(self)                 : Query board pixel height
 *   - LayeredVectorDrawable_getTypeId(self)                 : Query block type ID
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// draw/layered_vector_drawable.c — Multi-layer vector board implementation.

static void layeredVectorDrawableFreeStorage(LayeredVectorDrawable *self) {
    if (Memory_length(self) != 0)
        Memory_free(self);
    else
        free(self);
}

static LayeredVectorDrawable *layeredVectorDrawableCreate(uint32_t w, uint32_t h, size_t initialLayers) {
    if (w == 0 || h == 0)
        return nullptr;
    if (initialLayers == 0)
        initialLayers = 1;

    LayeredVectorDrawable *self = (LayeredVectorDrawable*) Memory_alloc(TYPE_LAYERED_VECTOR_DRAWABLE_SINGLETON, sizeof(LayeredVectorDrawable));
    if (!self)
        self = (LayeredVectorDrawable*) calloc(1, sizeof(LayeredVectorDrawable));
    if (!self)
        return nullptr;

    size_t cap = 4;
    while (cap < initialLayers)
        cap *= 2;

    LayeredVectorRow *rows = (LayeredVectorRow*) calloc(cap, sizeof(LayeredVectorRow));
    if (!rows) {
        layeredVectorDrawableFreeStorage(self);
        return nullptr;
    }

    size_t built = 0;
    for (; built < initialLayers; built++) {
        VectorDrawable *d = VectorDrawable_2(w, h);
        if (!d) {
            for (size_t j = 0; j < built; j++)
                VectorDrawable_free(rows[j].drawable);
            free(rows);
            layeredVectorDrawableFreeStorage(self);
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
    (*self).dirty = false;
    (*self).typeId = TYPE_LAYERED_VECTOR_DRAWABLE_SINGLETON;
    return self;
}

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

LayeredVectorDrawable *LayeredVectorDrawable_0(void) {
    return layeredVectorDrawableCreate(1, 1, 1);
}

LayeredVectorDrawable *LayeredVectorDrawable_2(uint32_t w, uint32_t h) {
    return layeredVectorDrawableCreate(w, h, 1);
}

LayeredVectorDrawable *LayeredVectorDrawable_3(uint32_t w, uint32_t h, size_t initialLayers) {
    return layeredVectorDrawableCreate(w, h, initialLayers);
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void LayeredVectorDrawable_free(LayeredVectorDrawable *self) {
    if (!self)
        return;
    if ((*self).layers) {
        for (size_t i = 0; i < (*self).layerCount; i++) {
            VectorDrawable_free((*self).layers[i].drawable);
            (*self).layers[i].drawable = nullptr;
        }
        free((*self).layers);
        (*self).layers = nullptr;
    }
    layeredVectorDrawableFreeStorage(self);
}

uint32_t LayeredVectorDrawable_addLayer(LayeredVectorDrawable *self) {
    if (!self)
        return UINT32_MAX;

    size_t count = (*self).layerCount;
    size_t cap = (*self).layerCapacity;
    if (count >= cap) {
        size_t newCap = cap == 0 ? 4 : cap * 2;
        LayeredVectorRow *newRows = (LayeredVectorRow*) realloc((*self).layers, newCap * sizeof(LayeredVectorRow));
        if (!newRows)
            return UINT32_MAX;
        memset(newRows + cap, 0, (newCap - cap) * sizeof(LayeredVectorRow));
        (*self).layers = newRows;
        (*self).layerCapacity = newCap;
    }

    VectorDrawable *d = VectorDrawable_2((*self).width, (*self).height);
    if (!d)
        return UINT32_MAX;

    uint32_t index = (uint32_t) count;
    LayeredVectorRow *row = &(*self).layers[index];
    (*row).drawable = d;
    (*row).opacity = 1.0f;
    (*row).blendMode = LAYER_BLEND_NORMAL;
    (*row).visible = true;
    (*self).layerCount = count + 1;
    (*self).activeIndex = index;
    (*self).dirty = true;

    return index;
}

bool LayeredVectorDrawable_removeLayer(LayeredVectorDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return false;

    LayeredVectorRow *rows = (*self).layers;
    VectorDrawable_free(rows[index].drawable);

    size_t moveCount = (*self).layerCount - index - 1;
    if (moveCount > 0)
        memmove(&rows[index], &rows[index + 1], moveCount * sizeof(LayeredVectorRow));
    size_t tail = (*self).layerCount - 1;
    memset(&rows[tail], 0, sizeof(LayeredVectorRow));

    (*self).layerCount = tail;
    if ((*self).activeIndex >= (*self).layerCount)
        (*self).activeIndex = ((*self).layerCount > 0) ? (uint32_t) ((*self).layerCount - 1) : 0;

    (*self).dirty = true;
    return true;
}

void LayeredVectorDrawable_render(LayeredVectorDrawable *self, Drawable *dest) {
    if (!self || !dest)
        return;
    for (size_t i = 0; i < (*self).layerCount; i++) {
        if (LayeredVectorDrawable_layerIsVisible(self, (uint32_t) i))
            VectorDrawable_render((*self).layers[i].drawable, dest);
    }
}

VectorDrawable *LayeredVectorDrawable_activeLayer(LayeredVectorDrawable *self) {
    if (!self || (*self).layerCount == 0)
        return nullptr;
    if ((*self).activeIndex >= (*self).layerCount)
        return nullptr;
    return (*self).layers[(*self).activeIndex].drawable;
}

VectorDrawable *LayeredVectorDrawable_layerGet(const LayeredVectorDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return nullptr;
    return (*self).layers[index].drawable;
}

void LayeredVectorDrawable_layerSetOpacity(LayeredVectorDrawable *self, uint32_t index, float opacity) {
    if (!self || index >= (*self).layerCount)
        return;
    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;
    (*self).layers[index].opacity = opacity;
    (*self).dirty = true;
}

float LayeredVectorDrawable_layerGetOpacity(const LayeredVectorDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return 0.0f;
    return (*self).layers[index].opacity;
}

void LayeredVectorDrawable_layerSetBlend(LayeredVectorDrawable *self, uint32_t index, uint32_t blendMode) {
    if (!self || index >= (*self).layerCount)
        return;
    (*self).layers[index].blendMode = blendMode;
    (*self).dirty = true;
}

uint32_t LayeredVectorDrawable_layerGetBlend(const LayeredVectorDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return 0;
    return (*self).layers[index].blendMode;
}

void LayeredVectorDrawable_layerSetVisible(LayeredVectorDrawable *self, uint32_t index, bool visible) {
    if (!self || index >= (*self).layerCount)
        return;
    (*self).layers[index].visible = visible;
    (*self).dirty = true;
}

bool LayeredVectorDrawable_layerIsVisible(const LayeredVectorDrawable *self, uint32_t index) {
    if (!self || index >= (*self).layerCount)
        return false;
    return (*self).layers[index].visible;
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void LayeredVectorDrawable_setActiveIndex(LayeredVectorDrawable *self, uint32_t activeIndex) {
    if (!self)
        return;
    if (activeIndex < (*self).layerCount || (activeIndex == 0 && (*self).layerCount == 0))
        (*self).activeIndex = activeIndex;
}

;;SETTER
void LayeredVectorDrawable_setVisibleMask(LayeredVectorDrawable *self, uint32_t visibleMask) {
    if (!self)
        return;
    size_t n = (*self).layerCount;
    if (n > 32u)
        n = 32u;
    for (size_t i = 0; i < n; i++)
        (*self).layers[i].visible = ((visibleMask & (1u << i)) != 0);
    (*self).dirty = true;
}

;;SETTER
void LayeredVectorDrawable_setWidth(LayeredVectorDrawable *self, uint32_t width) {
    if (!self)
        return;
    (*self).width = width;
    (*self).dirty = true;
}

;;SETTER
void LayeredVectorDrawable_setHeight(LayeredVectorDrawable *self, uint32_t height) {
    if (!self)
        return;
    (*self).height = height;
    (*self).dirty = true;
}

;;SETTER
void LayeredVectorDrawable_setSize(LayeredVectorDrawable *self, uint32_t w, uint32_t h) {
    if (!self)
        return;
    (*self).width = w;
    (*self).height = h;
    (*self).dirty = true;
}

;;SETTER
void LayeredVectorDrawable_setDirty(LayeredVectorDrawable *self, bool dirty) {
    if (!self)
        return;
    (*self).dirty = dirty;
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
uint32_t LayeredVectorDrawable_getActiveIndex(const LayeredVectorDrawable *self) {
    return self ? (*self).activeIndex : 0;
}

;;GETTER
uint32_t LayeredVectorDrawable_getVisibleMask(const LayeredVectorDrawable *self) {
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

;;GETTER
size_t LayeredVectorDrawable_getLayerCount(const LayeredVectorDrawable *self) {
    return self ? (*self).layerCount : 0;
}

;;GETTER
bool LayeredVectorDrawable_isDirty(const LayeredVectorDrawable *self) {
    return self ? (*self).dirty : false;
}

;;GETTER
uint32_t LayeredVectorDrawable_getWidth(const LayeredVectorDrawable *self) {
    return self ? (*self).width : 0;
}

;;GETTER
uint32_t LayeredVectorDrawable_getHeight(const LayeredVectorDrawable *self) {
    return self ? (*self).height : 0;
}

;;GETTER
uint64_t LayeredVectorDrawable_getTypeId(const LayeredVectorDrawable *self) {
    return self ? (*self).typeId : 0;
}
