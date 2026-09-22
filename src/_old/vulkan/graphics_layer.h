#ifndef VULKAN_GRAPHICS_LAYER_H
#define VULKAN_GRAPHICS_LAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// vulkan/graphics_layer.h — the graphics board shim (R3 driver, the Vertical Integration Law).
//
// One GraphicsLayer is one composited board in the window stack: the scene
// board (3D viewport) or the content board (UI canvas). It owns everything
// the GPU side needs per board — native pixel extent, backing scale, the
// opaque device handle, and the opaque platform layer handle — and nothing
// else. It never sees NSWindow, AppKit, or hotcwap's Window: both handles
// stay void* so the R3 -> R1 direction stays dependency-free (the Conflict Triage Law
// canonical move: opaque handle + callbacks, never a downstream #include).
//
// The window shim (hotcwap) stores two of these layers' platform handles as
// Window topLayer/bottomLayer void* slots and owns PARENTING ONLY
// (blur back, bottom, top front). This class owns CONTENT ONLY (device,
// drawableSize, resize pulse). Neither side dereferences the other's state.
//
// Platform layer factory lives in objc/graphics_layer_cocoa.m under the same
// GraphicsLayer_* prefix (same class, platform backend — not a second class
// per the Single Class Per File Law): GraphicsLayer_makeLayer / freeLayer / applySize / setDevice.

typedef struct GraphicsLayer GraphicsLayer;

// Board role: which stack slot this board fills. Fixed at construction.
#define GRAPHICS_LAYER_SCENE 0   // bottom board — 3D viewport / Vulkan scene
#define GRAPHICS_LAYER_CONTENT 1 // top board — UI canvas / Vulkan content

// Provenance: every live GraphicsLayer carries its registry id stamped at
// construction (graphvex/type.h ID_GRAPHICS_LAYER, next free number per the
// uniform per-project rule — never an out-of-registry window). The window
// shim stores bare void* and must never trust a handle it did not see come
// through the bind path below — a raw *ptr stuffed into the slot would be
// bridged to a CALayer and crash inside AppKit. GraphicsLayer_isValid
// (typeId gate) is the check; GraphicsLayer_bindWindow is the ONLY
// sanctioned attach path (graphvex-driven, backwards-initiated: the graphics
// side validates its own typed handle, then routes through the installed
// window seam — the window never pulls, never validates, never derefs).

// --- Overloaded constructors (the Vec4 chooser idiom) ---
//
//   GraphicsLayer()         -> scene board, 0x0, unattached
//   GraphicsLayer(role)     -> role board, 0x0, unattached
//
// Both construct DETACHED (layer == nullptr): attach via GraphicsLayer_attach.
GraphicsLayer *GraphicsLayer_0(void);
GraphicsLayer *GraphicsLayer_1(int role);

#define GRAPHICS_LAYER_CHOOSER(_0, _1, NAME, ...) NAME

#define GraphicsLayer(...) GRAPHICS_LAYER_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    GraphicsLayer_1, GraphicsLayer_0 \
)(__VA_ARGS__)

void GraphicsLayer_destroy(GraphicsLayer *self);

// Provenance gate: true only for a live constructor-issued handle (non-null
// + typeId == TYPE_GRAPHICS_LAYER_SINGLETON). False for nullptr, foreign
// structs, and use-after-destroy (destroy clears the tag before free). Cold
// path only — never on a frame.
bool GraphicsLayer_isValid(const GraphicsLayer *self);

// --- Window bind seam (backwards-initiated attach) -------------------------
//
// graphvex (R3) can never include hotcwap's window.h (the Vertical Integration Law), so the
// window side installs its two slot setters once at boot and the graphics
// side drives every attach through them. Flow: renderer creates
// GraphicsLayer -> makeLayer -> attach -> bindWindow; the window stores +
// parents the bare handle. Raw Window_setTop/BottomLayer stay public for
// ABI compat but are INTERNAL — call bindWindow, never the raw setters.
//
// The bind validates the TYPED handle (isValid: registry typeId gate) before anything
// crosses the seam, so a foreign *ptr can never reach the window slot: it
// fails here with false, far from any AppKit bridge.
typedef void (*GraphicsLayerWindowSetFn)(void *window, void *layer);
void GraphicsLayer_installWindowBind(GraphicsLayerWindowSetFn setTop,
                                     GraphicsLayerWindowSetFn setBottom);
bool GraphicsLayer_bindWindow(GraphicsLayer *self, void *window, int slot);

// --- Core functions ------------------------------------------------------
// Attach/detach the platform layer handle (created by
// GraphicsLayer_makeLayer, freed by GraphicsLayer_freeLayer). The handle is
// stored, never dereferenced. Detach does not free — the caller frees.
bool GraphicsLayer_attach(GraphicsLayer *self, void *layer);
void GraphicsLayer_detach(GraphicsLayer *self);

// Point-size chase (window points, live bounds): stores the size and raises
// the resize pulse when it actually moved. The caller converts to pixels via
// the stored scale and pushes them through GraphicsLayer_applySize.
bool GraphicsLayer_setPointSize(GraphicsLayer *self, int width, int height);
// Pixel-size commit (native pixels, scale already applied): stores the
// committed extent and lowers the resize pulse. Returns false on hostile
// input (nullptr, non-positive) with state untouched.
bool GraphicsLayer_setPixelSize(GraphicsLayer *self, int width, int height);
// Opaque device handle (MTLDevice*, stored never dereferenced). Pushed to
// the platform layer by the renderer via GraphicsLayer_setDevice.
void GraphicsLayer_setDevice(GraphicsLayer *self, void *device);

// --- Setters ---
void GraphicsLayer_setRole(GraphicsLayer *self, int role);
void GraphicsLayer_setScale(GraphicsLayer *self, float scale);
void GraphicsLayer_setImage(GraphicsLayer *self, void *vkImage);

// --- Getters (the Symmetric Getter/Setter Completeness Law: null-safe, defaults on nullptr) ---
void *GraphicsLayer_getLayer(const GraphicsLayer *self);
void *GraphicsLayer_getDevice(const GraphicsLayer *self);
void *GraphicsLayer_getImage(const GraphicsLayer *self);
int GraphicsLayer_getRole(const GraphicsLayer *self);
float GraphicsLayer_getScale(const GraphicsLayer *self);
bool GraphicsLayer_isAttached(const GraphicsLayer *self);
bool GraphicsLayer_needsResize(const GraphicsLayer *self);
void GraphicsLayer_getPointSize(const GraphicsLayer *self, int *outW, int *outH);
void GraphicsLayer_getPixelSize(const GraphicsLayer *self, int *outW, int *outH);

// --- Platform layer factory (implemented in objc/graphics_layer_cocoa.m on Apple) ---
void *GraphicsLayer_makeLayer(int role);
void  GraphicsLayer_freeLayer(void *layer);
bool  GraphicsLayer_applySize(void *layer, int pxW, int pxH, float scale);
void  GraphicsLayer_applyDevice(void *layer, void *device);
void  GraphicsLayer_transactionBegin(void);
void  GraphicsLayer_transactionCommit(void);
// Diagnostic read-back of a CAMetalLayer's CURRENT drawableSize (native px).
// Read-only; zeroes for a non-Metal layer. Used by the ANTI_RESIZE_TRACE
// resize probe to prove whether the per-step drawableSize write survives to
// the driver's surface-caps query.
void  GraphicsLayer_drawableSizeOf(void *layer, int *outW, int *outH);

#endif
