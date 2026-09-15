#include "vulkan/graphics_layer.h"

#include <stdlib.h>

#include "annotation/overview.h"
#include "graphvex/type.h"

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GraphicsLayer (vulkan/graphics_layer.c)
 * LEVEL: L2 — Behavior (per-board GPU shim state, the Four System Levels Law)
 * ============================================================================
 * One composited board in the window stack: the scene board (3D viewport,
 * bottom) or the content board (UI canvas, top). Owns the GPU side per
 * board — native pixel extent, backing scale, opaque device handle, opaque
 * platform layer handle — and nothing else. Never sees NSWindow, AppKit,
 * or hotcwap's Window: both handles stay void* (the Vertical Integration Law R3 sees vexspoke
 * only; the R3 -> R1 direction is void* + seam callbacks per the Conflict Triage Law).
 *
 * The window shim stores two platform handles as Window topLayer /
 * bottomLayer void* slots and owns PARENTING ONLY. This class owns CONTENT
 * ONLY (device, drawableSize, resize pulse). The resize pulse is the whole
 * live-resize contract: setPointSize raises it on movement, the renderer
 * converts points to pixels via scale, pushes them through the cocoa
 * applySize, then commits via setPixelSize which lowers it. Provenance is
 * the project type registry (graphvex/type.h ID_GRAPHICS_LAYER): the id is
 * stamped at construction and gated by GraphicsLayer_isValid on every cold
 * entry — no bespoke magic, same stamp rule as Swapchain's typeId.
 * Allocation is calloc-owned (Phase-1, like Application_0); the stamp, not
 * the allocator, carries provenance.
 *
 * STRUCT FIELDS (Mirroring vulkan/graphics_layer.h — exactly this file's class):
 * ----------------------------------------------------------------------------
 *   uint64_t typeId;      // TYPE_GRAPHICS_LAYER_SINGLETON while live, 0 after destroy
 *   void *layer;          // platform layer handle (CAMetalLayer*, stored never dereferenced)
 *   void *device;         // opaque device handle (MTLDevice*, stored never dereferenced)
 *   int role;             // GRAPHICS_LAYER_SCENE (bottom) or GRAPHICS_LAYER_CONTENT (top)
 *   float scale;          // backing scale factor (points -> pixels)
 *   int pointW;           // live bounds width in window points
 *   int pointH;           // live bounds height in window points
 *   int pixelW;           // committed extent width in native pixels
 *   int pixelH;           // committed extent height in native pixels
 *   bool attached;        // true once a platform layer is attached
 *   bool resizePending;   // true once point size moved past committed pixels
 *
 * WINDOW BIND SEAM (file-local, no struct — two installed fn pointers):
 * ----------------------------------------------------------------------------
 *   s_bindTop;            // window-side top slot setter (installed at boot)
 *   s_bindBottom;         // window-side bottom slot setter (installed at boot)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - GraphicsLayer()           : GraphicsLayer_0()
 *   - GraphicsLayer(role)       : GraphicsLayer_1(role)
 *
 * Core Functions:
 *   - GraphicsLayer_destroy(self)
 *   - GraphicsLayer_isValid(self)
 *   - GraphicsLayer_installWindowBind(setTop, setBottom)
 *   - GraphicsLayer_bindWindow(self, window, slot)
 *   - GraphicsLayer_attach(self, layer)
 *   - GraphicsLayer_detach(self)
 *   - GraphicsLayer_setPointSize(self, width, height)
 *   - GraphicsLayer_setPixelSize(self, width, height)
 *   - GraphicsLayer_setDevice(self, device)
 *
 * Setters:
 *   - GraphicsLayer_setRole(self, role)
 *   - GraphicsLayer_setScale(self, scale)
 *
 * Getters:
 *   - GraphicsLayer_getLayer(self)
 *   - GraphicsLayer_getDevice(self)
 *   - GraphicsLayer_getRole(self)
 *   - GraphicsLayer_getScale(self)
 *   - GraphicsLayer_isAttached(self)
 *   - GraphicsLayer_needsResize(self)
 *   - GraphicsLayer_getPointSize(self, outW, outH)
 *   - GraphicsLayer_getPixelSize(self, outW, outH)
 * ============================================================================
 */

typedef struct GraphicsLayer {
    uint64_t typeId;
    void *layer;
    void *device;
    int role;
    float scale;
    int pointW;
    int pointH;
    int pixelW;
    int pixelH;
    bool attached;
    bool resizePending;
} GraphicsLayer;

// Window bind seam: installed once by the window side at boot (hotcwap
// windowAlloc). nullptr until installed — bind fails closed before that.
static GraphicsLayerWindowSetFn s_bindTop = nullptr;
static GraphicsLayerWindowSetFn s_bindBottom = nullptr;

// CONSTRUCTORS
GraphicsLayer *GraphicsLayer_0(void) {
    return GraphicsLayer_1(GRAPHICS_LAYER_SCENE);
}

GraphicsLayer *GraphicsLayer_1(int role) {
    GraphicsLayer *self = (GraphicsLayer*) calloc(1, sizeof(GraphicsLayer));
    if (!self)
        return nullptr;
    (*self).typeId = TYPE_GRAPHICS_LAYER_SINGLETON;
    (*self).layer = nullptr;
    (*self).device = nullptr;
    (*self).role = (role == GRAPHICS_LAYER_CONTENT) ? GRAPHICS_LAYER_CONTENT : GRAPHICS_LAYER_SCENE;
    (*self).scale = 1.0f;
    (*self).pointW = 0;
    (*self).pointH = 0;
    (*self).pixelW = 0;
    (*self).pixelH = 0;
    (*self).attached = false;
    (*self).resizePending = false;
    return self;
}

// CORE FUNCTIONS
void GraphicsLayer_destroy(GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return;
    (*self).typeId = 0;
    (*self).layer = nullptr;
    (*self).device = nullptr;
    (*self).attached = false;
    free(self);
}

bool GraphicsLayer_isValid(const GraphicsLayer *self) {
    if (!self)
        return false;
    return (*self).typeId == TYPE_GRAPHICS_LAYER_SINGLETON;
}

void GraphicsLayer_installWindowBind(GraphicsLayerWindowSetFn setTop,
                                     GraphicsLayerWindowSetFn setBottom) {
    s_bindTop = setTop;
    s_bindBottom = setBottom;
}

bool GraphicsLayer_bindWindow(GraphicsLayer *self, void *window, int slot) {
    if (!GraphicsLayer_isValid(self))
        return false;
    if (!window)
        return false;
    if (!(*self).attached || !(*self).layer)
        return false;
    if (slot == GRAPHICS_LAYER_CONTENT) {
        if (!s_bindTop)
            return false;
        s_bindTop(window, (*self).layer);
        return true;
    }
    if (slot == GRAPHICS_LAYER_SCENE) {
        if (!s_bindBottom)
            return false;
        s_bindBottom(window, (*self).layer);
        return true;
    }
    return false;
}

bool GraphicsLayer_attach(GraphicsLayer *self, void *layer) {
    if (!GraphicsLayer_isValid(self))
        return false;
    if (!layer)
        return false;
    (*self).layer = layer;
    (*self).attached = true;
    (*self).resizePending = true;
    return true;
}

void GraphicsLayer_detach(GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return;
    (*self).layer = nullptr;
    (*self).attached = false;
    (*self).resizePending = false;
}

bool GraphicsLayer_setPointSize(GraphicsLayer *self, int width, int height) {
    if (!GraphicsLayer_isValid(self))
        return false;
    if (width <= 0)
        return false;
    if (height <= 0)
        return false;
    if (width == (*self).pointW && height == (*self).pointH)
        return true;
    (*self).pointW = width;
    (*self).pointH = height;
    (*self).resizePending = true;
    return true;
}

bool GraphicsLayer_setPixelSize(GraphicsLayer *self, int width, int height) {
    if (!GraphicsLayer_isValid(self))
        return false;
    if (width <= 0)
        return false;
    if (height <= 0)
        return false;
    (*self).pixelW = width;
    (*self).pixelH = height;
    (*self).resizePending = false;
    return true;
}

void GraphicsLayer_setDevice(GraphicsLayer *self, void *device) {
    if (!GraphicsLayer_isValid(self))
        return;
    (*self).device = device;
}

// SETTERS
void GraphicsLayer_setRole(GraphicsLayer *self, int role) {
    if (!GraphicsLayer_isValid(self))
        return;
    if (role != GRAPHICS_LAYER_SCENE && role != GRAPHICS_LAYER_CONTENT)
        return;
    (*self).role = role;
}

void GraphicsLayer_setScale(GraphicsLayer *self, float scale) {
    if (!GraphicsLayer_isValid(self))
        return;
    if (scale <= 0.0f)
        return;
    if ((*self).scale == scale)
        return;
    (*self).scale = scale;
    (*self).resizePending = true;
}

// GETTERS
void *GraphicsLayer_getLayer(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return nullptr;
    return (*self).layer;
}

void *GraphicsLayer_getDevice(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return nullptr;
    return (*self).device;
}

int GraphicsLayer_getRole(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return GRAPHICS_LAYER_SCENE;
    return (*self).role;
}

float GraphicsLayer_getScale(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return 1.0f;
    return (*self).scale;
}

bool GraphicsLayer_isAttached(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return false;
    return (*self).attached && (*self).layer != nullptr;
}

bool GraphicsLayer_needsResize(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return false;
    return (*self).resizePending;
}

void GraphicsLayer_getPointSize(const GraphicsLayer *self, int *outW, int *outH) {
    bool valid = GraphicsLayer_isValid(self);
    if (outW)
        *outW = valid ? (*self).pointW : 0;
    if (outH)
        *outH = valid ? (*self).pointH : 0;
}

void GraphicsLayer_getPixelSize(const GraphicsLayer *self, int *outW, int *outH) {
    bool valid = GraphicsLayer_isValid(self);
    if (outW)
        *outW = valid ? (*self).pixelW : 0;
    if (outH)
        *outH = valid ? (*self).pixelH : 0;
}
