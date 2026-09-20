#include "vulkan/graphics_layer.h"

#include <stdlib.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"
#include "graphvex/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: GraphicsLayer
 * ============================================================================
 * Hardware presentation board shim representing an individual composited layer
 * within the platform window stack, differentiating the background 3D viewport
 * scene board from the foreground 2D UI content board.
 *
 * Encapsulates the GPU-side presentation state per board, including native
 * pixel extents, display backing scale, opaque device context, and opaque platform
 * layer handles (such as CAMetalLayer on macOS). Operates under strict vertical
 * separation: window system shims (hotcwap) control window hierarchy parenting
 * without inspecting graphics driver handles, while GraphicsLayer manages content
 * resolution and resize pulses without depending on window system APIs. All window
 * bindings pass through validated type-provenance gates (TYPE_GRAPHICS_LAYER_SINGLETON).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: GraphicsLayer (vulkan/graphics_layer.c)
 * LEVEL: L2 — Behavior (per-board GPU shim state, the Four System Levels Law)
 * ============================================================================
 * SUMMARY:
 *   One composited board in the window stack: the scene board (3D viewport,
 *   bottom) or the content board (UI canvas, top). Owns the GPU presentation
 *   state per board (native pixel extent, backing scale, opaque device handle,
 *   opaque platform layer handle). The window shim manages parenting while
 *   GraphicsLayer manages content dimensions and resize pulses.
 *
 * STRUCT FIELDS (Mirroring vulkan/graphics_layer.h):
 * ----------------------------------------------------------------------------
 *   uint64_t typeId;      // TYPE_GRAPHICS_LAYER_SINGLETON while live, 0 after destroy
 *   void *layer;          // platform layer handle (CAMetalLayer*, stored never dereferenced)
 *   void *device;         // opaque device handle (MTLDevice*, stored never dereferenced)
 *   void *image;          // opaque Vulkan image handle (VkImage, stored never dereferenced)
 *   int role;             // GRAPHICS_LAYER_SCENE (bottom) or GRAPHICS_LAYER_CONTENT (top)
 *   float scale;          // backing scale factor (points to pixels)
 *   int pointW;           // live bounds width in window points
 *   int pointH;           // live bounds height in window points
 *   int pixelW;           // committed extent width in native pixels
 *   int pixelH;           // committed extent height in native pixels
 *   bool attached;        // true once a platform layer is attached
 *   bool resizePending;   // true once point size moved past committed pixels
 *
 * WINDOW BIND SEAM:
 * ----------------------------------------------------------------------------
 *   s_bindTop;            // window-side top slot setter (installed at boot)
 *   s_bindBottom;         // window-side bottom slot setter (installed at boot)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - GraphicsLayer_0(void)                              : Scene board, 0x0, detached
 *   - GraphicsLayer_1(role)                              : Specified role board, 0x0, detached
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - GraphicsLayer_destroy(self)                        : Clear provenance stamp and free layer memory
 *   - GraphicsLayer_isValid(self)                        : Validate typeId provenance stamp
 *   - GraphicsLayer_installWindowBind(setTop, setBottom) : Register window subsystem bind callbacks
 *   - GraphicsLayer_bindWindow(self, window, slot)       : Dispatch layer handle to window slot
 *   - GraphicsLayer_attach(self, layer)                  : Bind platform CAMetalLayer handle
 *   - GraphicsLayer_detach(self)                         : Unbind platform layer handle
 *   - GraphicsLayer_setPointSize(self, width, height)    : Update window point size and raise resize pulse
 *   - GraphicsLayer_setPixelSize(self, width, height)    : Commit native pixel extent and lower resize pulse
 *   - GraphicsLayer_setDevice(self, device)              : Bind opaque graphics device handle
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - GraphicsLayer_setRole(self, role)                  : Mutate board stack role
 *   - GraphicsLayer_setScale(self, scale)                : Mutate backing display scale factor
 *   - GraphicsLayer_setImage(self, vkImage)              : Bind active presentation image handle
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - GraphicsLayer_getLayer(self)                       : Query platform layer handle
 *   - GraphicsLayer_getDevice(self)                      : Query graphics device handle
 *   - GraphicsLayer_getImage(self)                       : Query presentation image handle
 *   - GraphicsLayer_getRole(self)                        : Query board stack role
 *   - GraphicsLayer_getScale(self)                       : Query display backing scale factor
 *   - GraphicsLayer_isAttached(self)                     : Query whether platform layer is bound
 *   - GraphicsLayer_needsResize(self)                    : Query whether resize pulse is pending
 *   - GraphicsLayer_getPointSize(self, outW, outH)       : Query bounds dimensions in window points
 *   - GraphicsLayer_getPixelSize(self, outW, outH)       : Query committed extent in native pixels
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

typedef struct GraphicsLayer {
    uint64_t typeId;
    void *layer;
    void *device;
    void *image;
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

// CONSTRUCTORS (PUBLIC & PRIVATE)

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
    (*self).image = nullptr;
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

// CORE FUNCTIONS (PUBLIC & PRIVATE)

void GraphicsLayer_destroy(GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return;
    (*self).typeId = 0;
    (*self).layer = nullptr;
    (*self).device = nullptr;
    (*self).image = nullptr;
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

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void GraphicsLayer_setRole(GraphicsLayer *self, int role) {
    if (!GraphicsLayer_isValid(self))
        return;
    if (role != GRAPHICS_LAYER_SCENE && role != GRAPHICS_LAYER_CONTENT)
        return;
    (*self).role = role;
}

;;SETTER
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

;;SETTER
void GraphicsLayer_setImage(GraphicsLayer *self, void *vkImage) {
    if (!GraphicsLayer_isValid(self))
        return;
    (*self).image = vkImage;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
void *GraphicsLayer_getLayer(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return nullptr;
    return (*self).layer;
}

;;GETTER
void *GraphicsLayer_getDevice(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return nullptr;
    return (*self).device;
}

;;GETTER
void *GraphicsLayer_getImage(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return nullptr;
    return (*self).image;
}

;;GETTER
int GraphicsLayer_getRole(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return GRAPHICS_LAYER_SCENE;
    return (*self).role;
}

;;GETTER
float GraphicsLayer_getScale(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return 1.0f;
    return (*self).scale;
}

;;GETTER
bool GraphicsLayer_isAttached(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return false;
    return (*self).attached && (*self).layer != nullptr;
}

;;GETTER
bool GraphicsLayer_needsResize(const GraphicsLayer *self) {
    if (!GraphicsLayer_isValid(self))
        return false;
    return (*self).resizePending;
}

;;GETTER
void GraphicsLayer_getPointSize(const GraphicsLayer *self, int *outW, int *outH) {
    bool valid = GraphicsLayer_isValid(self);
    if (outW)
        *outW = valid ? (*self).pointW : 0;
    if (outH)
        *outH = valid ? (*self).pointH : 0;
}

;;GETTER
void GraphicsLayer_getPixelSize(const GraphicsLayer *self, int *outW, int *outH) {
    bool valid = GraphicsLayer_isValid(self);
    if (outW)
        *outW = valid ? (*self).pixelW : 0;
    if (outH)
        *outH = valid ? (*self).pixelH : 0;
}
